"""
S800 串口测试自动执行器 + 自动评分器
用法:
  python test_runner.py                          # 交互式引导
  python test_runner.py --student-id 202412345678 --port COM3
  python test_runner.py --score testcmd_log_xxx.txt  # 仅对已有日志评分
"""

import serial
import serial.tools.list_ports
import time
import sys
import os
import re
from datetime import datetime

# ── 配置 ──────────────────────────────────────────────
BAUDRATE        = 115200
RESPONSE_TIMEOUT = 3.0   # 等待第一条应答最长秒数
POST_DRAIN      = 0.1    # 收到应答后再捞 EVT 的窗口（秒）
DEFAULT_DELAY   = 0.1    # 命令间固定间隔（秒），最大 0.5
MAX_DELAY       = 0.5
# ──────────────────────────────────────────────────────

# ── 评分规则表 ─────────────────────────────────────────
# 每条: (行号, 检查类型, 说明)
# 检查类型:
#   ok          → 应答以 "OK" 开头
#   ok_data     → 应答以 "OK " 开头且后有内容
#   error       → 应答以 "ERROR" 开头
#   pong        → 应答以 "*PONG " 开头
#   ok_left12   → LEFT模式时间正序：应答以 "OK 12." 开头（已知hour=12）
#   ok_right12  → RIGHT模式时间逆序：应答匹配 "OK \d\d\.\d\d\.21"（hour=12→21）
SCORE_CATEGORIES = [
    {
        "id": "case", "name": "大小写不敏感", "max": 2.0,
        "rules": [
            (26, "ok",      "全小写 *set:time..."),
            (27, "ok",      "混合大小写 *Set:Time..."),
            (28, "ok",      "随机混合"),
            (29, "ok_data", "*get:TIME"),
            (30, "pong",    "*ping"),
            (31, "ok",      "*rst"),
        ]
    },
    {
        "id": "space", "name": "空格容错", "max": 2.0,
        "rules": [
            (33, "ok",      "双空格 SET:DATE"),
            (34, "ok",      "三空格 SET:TIME"),
            (35, "ok_data", "*GET:  TIME"),
            (36, "pong",    "*PING  (尾空格)"),
        ]
    },
    {
        "id": "abbrev", "name": "缩写合法", "max": 2.0,
        "rules": [
            (38, "ok",    "MIN (合法最小缩写)"),
            (39, "ok",    "MINU (合法)"),
            (40, "ok",    "MINUT (合法)"),
            (41, "ok",    "DISP (合法最小缩写)"),
            (42, "ok",    "DISPL (合法)"),
            (43, "ok",    "ALARM中 MIN/SEC"),
            (44, "error", "MONT (非法！MONTH全大写不可缩写) → 应返回ERROR"),
            (45, "error", "MI   (非法！低于MIN最小展开) → 应返回ERROR"),
        ]
    },
    {
        "id": "multi", "name": "多参数组合≥3种", "max": 2.0,
        "rules": [
            (47, "ok",      "三参: YEAR MONTH DATE"),
            (49, "ok",      "双参: YEAR DATE"),
            (50, "ok",      "双参: MONTH DATE"),
            (51, "ok",      "单参: YEAR"),
            (52, "ok",      "双参: HOUR SEC"),
            (53, "ok",      "双参: HOUR MIN"),
        ]
    },
    {
        "id": "format", "name": "FORMAT RIGHT逆序+小数点", "max": 2.0,
        "rules": [
            (57, "ok",         "SET:FORMAT LEFT"),
            (58, "ok_left12",  "LEFT查时间 → 以'12.'开头"),
            (59, "ok",         "SET:FORMAT RIGHT"),
            (60, "ok_right12", "RIGHT查时间 → 以'.21'结尾(hour=12逆序)"),
            (61, "ok_data",    "RIGHT查日期 → 有数据"),
            (62, "ok",         "SET:FORMAT LEFT"),
            (63, "ok_left12",  "LEFT恢复 → 以'12.'开头"),
        ]
    },
]
# ──────────────────────────────────────────────────────


def prompt_student_id(given=None):
    """验证或交互获取12位数字学号"""
    def validate(s):
        return bool(re.fullmatch(r"\d{12}", s.strip()))

    if given:
        if validate(given):
            return given.strip()
        print(f"  ✗ 学号格式错误：'{given}'（需12位纯数字）")

    print("\n" + "="*50)
    print("  S800 串口协议测试程序")
    print("="*50)
    while True:
        sid = input("  请输入学号（12位数字）: ").strip()
        if validate(sid):
            return sid
        print(f"  ✗ 格式错误，需要恰好12位数字，请重新输入")


def prompt_port(given=None):
    """交互选择串口"""
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("  ✗ 未检测到任何串口！请检查设备连接")
        return None

    # 自动优先USB串口
    usb_ports = [p for p in ports if "USB" in (p.description or "").upper()]
    auto = usb_ports[0].device if usb_ports else ports[0].device

    if given:
        return given

    print("\n  检测到以下串口：")
    for i, p in enumerate(ports):
        tag = " ← 推荐" if p.device == auto else ""
        print(f"    [{i+1}] {p.device}  {p.description}{tag}")

    choice = input(f"  请选择串口编号 (直接回车使用推荐 {auto}): ").strip()
    if not choice:
        return auto
    try:
        idx = int(choice) - 1
        if 0 <= idx < len(ports):
            return ports[idx].device
    except ValueError:
        pass
    # 用户直接输入了COM号
    return choice


def open_serial(port):
    ser = serial.Serial(port, BAUDRATE, timeout=0.05)
    time.sleep(0.1)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


def drain_response(ser, timeout_s=RESPONSE_TIMEOUT, post_s=POST_DRAIN):
    """
    等待并捕获命令应答。
    阶段1: 最多等 timeout_s 秒，收到第一条非EVT行即停。
    阶段2: 再等 post_s 秒捞剩余EVT。
    返回 (responses, evts)
    """
    responses = []
    evts = []

    # 阶段1：等待应答
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            time.sleep(0.005)
            continue
        line = raw.decode("ascii", errors="replace").strip()
        if not line:
            continue
        if line.startswith("*EVT:"):
            evts.append(line)
        else:
            responses.append(line)
            break  # 收到应答

    # 阶段2：捞EVT尾巴
    drain_dl = time.time() + post_s
    while time.time() < drain_dl:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="replace").strip()
        if not line:
            continue
        if line.startswith("*EVT:"):
            evts.append(line)
        else:
            responses.append(line)

    return responses, evts


def send_and_capture(ser, cmd, line_no, log_lines, inter_delay):
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    log_lines.append(f"[{ts}] >> L{line_no:03d} {cmd}")
    print(f"  L{line_no:03d} >> {cmd}")

    ser.write((cmd + "\r\n").encode("ascii"))
    ser.flush()

    responses, evts = drain_response(ser)

    if responses:
        for r in responses:
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            log_lines.append(f"[{ts}] << {r}")
            print(f"         << {r}")
    else:
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_lines.append(f"[{ts}] << [TIMEOUT] 无应答")
        print(f"         << [TIMEOUT]")

    for e in evts:
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_lines.append(f"[{ts}] ~~ {e}")

    time.sleep(inter_delay)
    return responses


# ── 评分引擎 ──────────────────────────────────────────

def parse_log(log_path):
    """解析日志，返回 {line_no: [response_str, ...]} 映射"""
    result = {}
    current_line = None
    with open(log_path, "r", encoding="utf-8") as f:
        for row in f:
            row = row.strip()
            # 发送行：>> L003 *RST
            m = re.match(r"\[[\d:.]+\] >> L(\d+) ", row)
            if m:
                current_line = int(m.group(1))
                result.setdefault(current_line, [])
                continue
            # 应答行：<< OK ...
            m = re.match(r"\[[\d:.]+\] << (.+)", row)
            if m and current_line is not None:
                result[current_line].append(m.group(1).strip())
    return result


def check_response(resp_list, check_type):
    """
    对某行的应答列表做类型检查，返回 (passed: bool, detail: str)
    """
    if not resp_list:
        return False, "无应答(TIMEOUT)"

    # 取第一条非空应答
    resp = resp_list[0].strip()

    if check_type == "ok":
        ok = resp.upper().startswith("OK")
        return ok, resp

    elif check_type == "ok_data":
        ok = resp.upper().startswith("OK ") and len(resp) > 3
        return ok, resp

    elif check_type == "error":
        ok = resp.upper().startswith("ERROR")
        return ok, resp

    elif check_type == "pong":
        ok = resp.upper().startswith("*PONG ")
        return ok, resp

    elif check_type == "ok_left12":
        # LEFT模式，时间以 "OK 12." 开头（已知hour固定为12）
        ok = bool(re.match(r"OK 12\.", resp, re.IGNORECASE))
        return ok, resp

    elif check_type == "ok_right12":
        # RIGHT模式，时间 "OK XX.XX.21"（hour=12逆序=21）
        ok = bool(re.match(r"OK \d{2}\.\d{2}\.21$", resp, re.IGNORECASE))
        return ok, resp

    return False, f"未知检查类型: {check_type}"


def score_log(log_path):
    """对日志文件自动评分，返回评分报告字符串"""
    responses = parse_log(log_path)

    lines = []
    lines.append("=" * 60)
    lines.append("  串口协议自动评分报告")
    lines.append(f"  日志: {os.path.basename(log_path)}")
    lines.append("=" * 60)

    total_score = 0.0
    total_max = 0.0

    for cat in SCORE_CATEGORIES:
        cat_pass = 0
        cat_total = len(cat["rules"])
        cat_details = []

        for line_no, check_type, desc in cat["rules"]:
            resp_list = responses.get(line_no, [])
            passed, detail = check_response(resp_list, check_type)
            mark = "✅" if passed else "❌"
            cat_details.append(f"    L{line_no:03d} {mark} {desc}  [{detail}]")
            if passed:
                cat_pass += 1

        # 按通过比例计算得分
        cat_score = round(cat["max"] * cat_pass / cat_total, 2) if cat_total else 0
        total_score += cat_score
        total_max += cat["max"]

        lines.append("")
        lines.append(f"【{cat['name']}】  {cat_pass}/{cat_total} 通过  "
                     f"→ {cat_score:.1f}/{cat['max']:.1f} 分")
        lines.extend(cat_details)

    lines.append("")
    lines.append("─" * 60)
    lines.append(f"  串口协议小计: {total_score:.1f} / {total_max:.1f} 分")
    lines.append("─" * 60)

    # 额外观察项（不计分，但标注问题）
    lines.append("")
    lines.append("【额外观察（不计入本项评分）】")
    extra_checks = [
        (69, "error", "*SET:MSG 无参数→应返回ERROR"),
    ]
    for line_no, check_type, desc in extra_checks:
        resp_list = responses.get(line_no, [])
        passed, detail = check_response(resp_list, check_type)
        mark = "✅" if passed else "⚠️"
        lines.append(f"    L{line_no:03d} {mark} {desc}  [{detail}]")

    lines.append("")
    lines.append("=" * 60)
    return "\n".join(lines), total_score, total_max


# ── 主程序 ────────────────────────────────────────────

def run_test(student_id, port, cmd_file, inter_delay):
    """执行测试并生成日志"""
    if not os.path.isfile(cmd_file):
        print(f"\n  ✗ 找不到命令文件: '{cmd_file}'")
        sys.exit(1)

    with open(cmd_file, "r", encoding="utf-8") as f:
        cmds = [line.strip() for line in f if line.strip()]

    print(f"\n  命令文件: {cmd_file}  共 {len(cmds)} 条")
    print(f"  命令间隔: {inter_delay:.2f}s")
    print(f"  学号: {student_id}")

    try:
        ser = open_serial(port)
        print(f"  串口已打开: {port}\n")
    except serial.SerialException as e:
        print(f"\n  ✗ 无法打开串口 {port}: {e}")
        sys.exit(1)

    log_lines = []
    start = datetime.now()
    log_lines += [
        f"# S800 串口测试日志",
        f"# 学号: {student_id}",
        f"# 时间: {start.strftime('%Y-%m-%d %H:%M:%S')}",
        f"# 串口: {port}  波特率: {BAUDRATE}",
        f"# 命令文件: {cmd_file}  共 {len(cmds)} 条",
        f"# 命令间隔: {inter_delay:.2f}s",
        f"# >> 发送  << 应答  ~~ EVT事件",
        "",
    ]

    ok_cnt = err_cnt = to_cnt = 0
    try:
        for i, cmd in enumerate(cmds, start=1):
            resps = send_and_capture(ser, cmd, i, log_lines, inter_delay)
            if not resps:
                to_cnt += 1
            elif any(r.upper().startswith("ERROR") for r in resps):
                err_cnt += 1
            else:
                ok_cnt += 1
    except KeyboardInterrupt:
        print("\n\n  用户中断测试")
    finally:
        ser.close()

    end = datetime.now()
    duration = (end - start).total_seconds()
    log_lines += [
        "",
        f"# 测试结束: {end.strftime('%Y-%m-%d %H:%M:%S')}",
        f"# 耗时: {duration:.1f}s  OK:{ok_cnt}  ERROR:{err_cnt}  TIMEOUT:{to_cnt}",
    ]

    log_name = f"testcmd_log_{student_id}_{end.strftime('%Y%m%d_%H%M%S')}.txt"
    log_path = os.path.join(os.path.dirname(os.path.abspath(cmd_file)), log_name)
    with open(log_path, "w", encoding="utf-8") as f:
        f.write("\n".join(log_lines) + "\n")

    print(f"\n{'='*50}")
    print(f"  测试完成  OK:{ok_cnt}  ERROR:{err_cnt}  TIMEOUT:{to_cnt}")
    print(f"  日志: {log_path}")
    return log_path


def main():
    import argparse
    parser = argparse.ArgumentParser(description="S800 串口测试执行器+评分器")
    parser.add_argument("--student-id", default=None,
                        help="12位学号（缺省则交互输入）")
    parser.add_argument("--port", default=None,
                        help="串口号如 COM3（缺省则交互选择）")
    parser.add_argument("--file", default="testcmd.txt",
                        help="命令文件，默认 testcmd.txt")
    parser.add_argument("--delay", type=float, default=DEFAULT_DELAY,
                        help=f"命令间隔秒，默认 {DEFAULT_DELAY}，最大 {MAX_DELAY}")
    parser.add_argument("--score", metavar="LOG_FILE", default=None,
                        help="仅对已有日志文件评分，不执行测试")
    args = parser.parse_args()

    # ── 仅评分模式 ──
    if args.score:
        if not os.path.isfile(args.score):
            print(f"错误: 找不到日志文件 '{args.score}'")
            sys.exit(1)
        report, score, max_score = score_log(args.score)
        print(report)
        # 将报告追加写入日志旁边
        report_path = args.score.replace(".txt", "_score.txt")
        with open(report_path, "w", encoding="utf-8") as f:
            f.write(report + "\n")
        print(f"\n  评分报告已保存: {report_path}")
        return

    # ── 执行测试模式 ──
    student_id = prompt_student_id(args.student_id)
    delay = min(max(args.delay, 0.0), MAX_DELAY)
    port = prompt_port(args.port)
    if not port:
        sys.exit(1)

    input(f"\n  按 Enter 开始测试（共 100 条命令，约 {100*(delay+0.5):.0f} 秒）...")
    print()

    log_path = run_test(student_id, port, args.file, delay)

    # 测试完成后自动评分
    print(f"\n  正在自动评分...\n")
    report, score, max_score = score_log(log_path)
    print(report)

    report_path = log_path.replace(".txt", "_score.txt")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write(report + "\n")
    print(f"  评分报告已保存: {report_path}")


if __name__ == "__main__":
    main()
