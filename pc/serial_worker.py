# -*- coding: utf-8 -*-
"""QThread‑based serial worker for S800 clock."""

import time
from typing import Optional, List

import serial
import serial.tools.list_ports
from PyQt5.QtCore import QObject, pyqtSignal, QMutex


def scan_ports() -> List[dict]:
    ports = []
    for p in serial.tools.list_ports.comports():
        ports.append({
            "port": p.device,
            "description": p.description,
            "vid": p.vid,
            "pid": p.pid,
        })
    return ports


class SerialWorker(QObject):
    """Lives in a QThread.  Owns the serial port and runs the read loop."""

    line_received = pyqtSignal(str)
    error_occurred = pyqtSignal(str)
    connection_changed = pyqtSignal(bool, str)
    pong_received = pyqtSignal(int)   # uptime_seconds from *PONG reply
    tx_activity = pyqtSignal()        # emitted after each successful send

    def __init__(self, parent=None):
        super().__init__(parent)
        self._serial: Optional[serial.Serial] = None
        self._active = False
        self._quit = False
        self._buffer = b""
        self._mutex = QMutex()
        self._port_name = ""
        self._baudrate = 115200

    # -- public API (called from main thread) --

    def open_port(self, port: str, baudrate: int = 115200) -> bool:
        self._port_name = port
        self._baudrate = baudrate
        try:
            self._serial = serial.Serial(
                port=port,
                baudrate=baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.05,
                write_timeout=0.5,
            )
            self._active = True
            self._buffer = b""
            self.connection_changed.emit(True, port)
            return True
        except Exception as exc:
            self._serial = None
            self._active = False
            self.error_occurred.emit(f"无法打开串口 {port}: {exc}")
            return False

    def close_port(self):
        self._active = False
        self._mutex.lock()
        try:
            if self._serial and self._serial.is_open:
                self._serial.close()
        except Exception:
            pass
        finally:
            self._mutex.unlock()
        self._serial = None
        self.connection_changed.emit(False, self._port_name)

    def send(self, text: str):
        self._mutex.lock()
        try:
            if self._serial and self._serial.is_open:
                line = (text.strip() + "\r\n").encode("ascii", errors="replace")
                self._serial.write(line)
                self._serial.flush()
                self.tx_activity.emit()
        except (OSError, serial.SerialException) as exc:
            self.error_occurred.emit(f"发送失败: {exc}")
        finally:
            self._mutex.unlock()

    def is_connected(self) -> bool:
        return self._serial is not None and self._serial.is_open

    # -- background read loop --

    def run_loop(self):
        while not self._quit:
            if not self._active:
                time.sleep(0.1)
                continue

            try:
                self._mutex.lock()
                ser = self._serial
                self._mutex.unlock()

                if ser is None or not ser.is_open:
                    time.sleep(0.1)
                    continue

                waiting = ser.in_waiting
                if waiting > 0:
                    data = ser.read(waiting)
                    self._buffer += data
                    self._extract_lines()

                time.sleep(0.02)

            except (OSError, serial.SerialException) as exc:
                self.error_occurred.emit(f"串口读取异常: {exc}")
                self._active = False
                self.connection_changed.emit(False, self._port_name)

    def _extract_lines(self):
        if len(self._buffer) > 4096:
            self.error_occurred.emit(
                f"缓冲区溢出 ({len(self._buffer)} 字节无行终止符), 已清空")
            self._buffer = b""
            return
        while True:
            idx_n = self._buffer.find(b"\n")
            idx_r = self._buffer.find(b"\r")
            if idx_n < 0 and idx_r < 0:
                break
            if idx_n < 0:
                idx = idx_r
            elif idx_r < 0:
                idx = idx_n
            else:
                idx = min(idx_n, idx_r)

            raw = self._buffer[:idx]
            term = self._buffer[idx:idx + 1]
            self._buffer = self._buffer[idx + 1:]

            if term == b"\r" and self._buffer.startswith(b"\n"):
                self._buffer = self._buffer[1:]

            line = raw.decode("latin-1", errors="replace").strip()
            if line:
                self._process_line(line)

    def _process_line(self, line: str):
        upper = line.upper()

        if upper.startswith("*PONG"):
            try:
                uptime = int(line[5:].strip())
            except ValueError:
                uptime = 0
            self.pong_received.emit(uptime)
            self.line_received.emit(line)
            return

        if upper.startswith("ERROR"):
            self.error_occurred.emit(f"S800: {line}")
            self.line_received.emit(line)
            return

        self.line_received.emit(line)

    def stop(self):
        self._quit = True
        self._active = False
        self.close_port()
