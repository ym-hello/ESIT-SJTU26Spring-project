# ESIT-SJTU26Spring-project

S800 时钟项目 — 基于 TM4C1294NCPDT 的桌面时钟系统，包含 MCU 固件和 PC 上位机。

| 组件 | 目录 | 说明 |
|------|------|------|
| MCU 固件 | `/` (仓库根目录) | TM4C1294NCPDT (ARM Cortex-M4F) 固件，Keil MDK 工程 |
| PC 上位机 | [`pc/`](pc/) | PyQt5 桌面应用，串口通信、数字孪生、数据面板 |
| 原始源代码 | [`code.zip`](code.zip) | 提交 AI 整理前的原始 MCU+PC 源代码（含未拆分 `main.c`） |

> **注意**：当前仓库的 MCU 固件代码已经过模块化拆分重构。如需未经改动的原始代码，可直接使用 [`code.zip`](code.zip)。

---

## 仓库结构

```
/
├── main.c                  # 主入口 + 状态机（精简后）
├── src/                    # MCU 固件模块源文件
│   ├── i2c_drv.c           #   I2C 底层读写
│   ├── init.c              #   外设初始化
│   ├── seg7.c              #   7段字模转换
│   ├── buttons.c           #   按键去抖读取
│   ├── display.c           #   显示扫描、时钟渲染、滚动
│   ├── led.c               #   LED 状态合成
│   ├── alarm.c             #   闹钟状态机、日期工具
│   ├── edit.c              #   编辑模式增量
│   ├── serial.c            #   UART 通信
│   └── cmd.c               #   串口命令处理
├── inc/                    # TivaWare 外设头文件 + 模块头文件
│   ├── board.h             #   集中定义、枚举、extern 声明
│   ├── serial.h            #   串口模块头文件
│   ├── display.h           #   显示模块头文件
│   ├── (led.h, alarm.h, ...)   # 各模块头文件
│   └── hw_*.h              #   TivaWare 硬件头文件
├── driverlib/              # TivaWare 驱动库源码
├── RTE/                    # Keil Run-Time Environment 配置
├── hw.uvprojx              # Keil μVision 项目文件
├── hw.uvoptx               # Keil 选项配置
├── pc/                     # PC 上位机
│   ├── main.py             #   应用入口
│   ├── ui_main.py          #   主窗口 (PyQt5, 1939行)
│   ├── protocol.py         #   串口协议解析
│   ├── serial_worker.py    #   串口通信线程
│   ├── event_logger.py     #   CSV 事件日志
│   ├── test/               #   测试脚本
│   ├── requirements.txt    #   Python 依赖
│   └── README.md           #   上位机详细说明
├── code.zip                # AI 重构前的原始源代码
├── Listings/               # 编译清单文件 (自动生成)
├── Objects/                # 编译目标文件 (自动生成)
└── README.md               # 本文件
```

---

## 硬件

### 下位机 (MCU)

| 项目 | 规格 |
|------|------|
| **主控** | TM4C1294NCPDT (ARM Cortex-M4F @ 120 MHz) |
| **显示** | 8 位 7 段数码管 (通过 I²C 驱动) |
| **指示灯** | 8 颗 LED (PCA9557, I²C 地址 `0x18`) |
| **通信** | UART (默认 115200 bps) |
| **按键** | FUNC / SHIFT / ADD / SAVE / DISP / SPEED / FORMAT / EXT / USER1 / USER2 |
| **时钟源** | 外部 25 MHz 晶振 |
| **开发板** | TM4C1294 Connected LaunchPad (EK-TM4C1294XL) |

LED 位定义详见 [MCU 固件 LED 说明](#led-位定义)。

### 上位机 (PC)

- **系统要求**：Windows（依赖 pywin32 / comtypes / winrt）
- **Python**：3.11+
- **串口**：任意可用 COM 口（需安装对应驱动，如 CH340 / CP2102 / 板载 ICDI 虚拟串口）

---

## 快速开始

### 1. 克隆仓库

```bash
git clone git@github.com:ym-hello/ESIT-SJTU26Spring-project.git
cd ESIT-SJTU26Spring-project
```

### 2. MCU 固件 — 编译 & 烧录

#### 工具链

- **Keil MDK v5**（推荐）— 直接打开 `hw.uvprojx`
- **ARM Compiler v5 / v6**（MDK 内置）
- TivaWare 驱动库已包含在 `driverlib/` 中，无需额外安装

#### 编译

1. 用 Keil μVision 打开 `hw.uvprojx`
2. 在 Project 窗口选择目标配置（Debug / Release）
3. 按 **F7** 或菜单 **Project → Build Target**
4. 生成文件位于 `Objects/` 目录

#### 烧录

- **Keil MDK**：按 **Ctrl+F5** (Start Debug) 或 **F8** (Load) 通过调试器烧录
- **LM Flash Programmer**：使用 `Objects/hw.axf` 或导出的 `.bin` 文件烧录
- **OpenOCD + GDB**：也可用于烧录和调试

#### 连接方式

EK-TM4C1294XL 开发板通过 USB 连接 PC，板载 ICDI 调试器同时提供调试接口和虚拟串口。

### 3. PC 上位机 — 安装 & 运行

```bash
# 进入 pc/ 目录
cd pc

# 创建并激活虚拟环境
python -m venv .venv

# Windows CMD / PowerShell
.venv\Scripts\activate

# 安装依赖
pip install -r requirements.txt

# 运行
python main.py
```

详细使用说明见 [pc/README.md](pc/README.md)。

---

## 上位机功能

| 功能 | 说明 |
|------|------|
| **串口通信** | 自动扫描可用串口，支持多种波特率，收发日志实时显示 |
| **数字孪生** | 8 位七段数码管、8 颗 LED、虚拟按键，实时同步下位机状态 |
| **时钟控制** | 读取/设置日期、时间、闹钟，切换日间/夜间显示模式 |
| **天气推送** | 通过 wttr.in API 获取天气数据并下发到下位机 |
| **NTP 对时** | 通过 NTP 服务器获取标准时间，同步下位机 RTC |
| **语音播报** | 基于 winrt TTS 的事件语音播报（仅 Windows） |
| **数据看板** | matplotlib 图表展示闹钟分布、NTP 精度、按键热度 |
| **日志导出** | TXT / CSV 格式收发日志导出 |
| **日夜自动切换** | 根据上海地区经纬度计算日出日落时间，自动切换显示模式 |

---

## LED 位定义

| 位 | LED | 标签 | 含义 | 行为 |
|----|-----|------|------|------|
| 0 | LED0 | HB | 系统心跳 | 1 Hz 闪烁 |
| 1 | LED1 | ALM | 闹钟状态 | 使能时常亮；响铃时快闪 |
| 2 | LED2 | EDT | 编辑模式 | 进入编辑状态时常亮 |
| 3 | LED3 | UART | 串口收发 | 活动后保持 100ms |
| 4 | LED4 | WX_SUN | 晴天指示 | PC 天气数据控制 |
| 5 | LED5 | WX_RAIN | 雨雪指示 | 1 Hz 呼吸（PC 天气推送） |
| 6 | LED6 | TEMP | 高温告警 | ≥ 30°C 常亮（预留） |
| 7 | LED7 | NTP | 对时同步 | 同步中 1Hz 闪，完成常亮 3s |

---

## 串口协议

通信基于 ASCII 文本行（`\r\n` 或 `\n` 终止），所有命令以 `*` 开头。

### 基本命令

| 命令 | 参数 | 说明 |
|------|------|------|
| `*PING` | — | 心跳检测，回复 `*PONG <秒>` |
| `*RST` | — | 复位下位机 |
| `*SET:TIME` | `HOUR <h> MIN <m> SEC <s>` | 设置时间 |
| `*SET:DATE` | `YEAR <y> MONTH <m> DATE <d>` | 设置日期 |
| `*SET:ALARM` | `HOUR <h> MIN <m> SEC <s>` | 设置闹钟 |
| `*SET:ALARM` | `OFF` | 关闭闹钟 |
| `*SET:KEY` | `<KEY_NAME>` | 模拟按键 |
| `*SET:DISPLAY` | `ON` / `OFF` | 开关显示 |
| `*SET:FORMAT` | `LEFT` / `RIGHT` | 设置时间显示格式 |
| `*SET:MODE` | `DAY` / `NIGHT` | 日/夜间模式切换 |
| `*SET:MSG` | `<文本>` | 滚动消息显示 |
| `*SET:BEEP` | `<ms>` | 蜂鸣器控制 |
| `*SET:LED` | `<hex>` | 直接设置 LED |
| `*SET:WEA` | `<temp> <SUN/CLD/OVC/RAI/SNO/FOG>` | 天气推送 |
| `*SET:MSG` | `<文本>` | 滚动消息 |

### 主动上报

下位机以 `*EVT:` 开头的事件消息定时上报当前状态（~1 Hz）：

| 消息 | 说明 |
|------|------|
| `*EVT:TIME: <HH:MM:SS>` | 当前时间 |
| `*EVT:DATE: <YYYY-MM-DD>` | 当前日期 |
| `*EVT:ALARM: <HH:MM:SS> ON/OFF` | 闹钟状态 |
| `*EVT:DISPLAY: ON/OFF` | 显示开关状态 |
| `*EVT:FORMAT: LEFT/RIGHT` | 显示格式 |
| `*EVT:MODE: DAY/NIGHT` | 日/夜间模式 |
| `*EVT:DISP:<data>:<dp>` | 7 段显示数据 |
| `*EVT:LED:<hex>` | LED 状态 |
| `*EVT:KEY:<name>` | 按键事件 |

---

## 许可

本项目代码仅用于 ESIT-SJTU26Spring 课程实验目的。
