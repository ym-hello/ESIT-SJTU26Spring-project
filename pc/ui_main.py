# -*- coding: utf-8 -*-
"""S800 PC Host — main window, panels, and custom widgets."""

import matplotlib
matplotlib.rcParams["font.sans-serif"] = ["SimHei", "Microsoft YaHei", "DejaVu Sans"]
matplotlib.rcParams["axes.unicode_minus"] = False

import time
import threading
from datetime import datetime
from typing import Optional

from astral import LocationInfo
from astral.sun import sun

from PyQt5.QtCore import Qt, QTimer, pyqtSignal, QPointF, QThread
from PyQt5.QtGui import QPainter, QColor, QPen, QBrush, QFont, QPolygonF
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGridLayout, QFormLayout, QSplitter, QGroupBox, QLabel,
    QPushButton, QComboBox, QLineEdit, QRadioButton, QButtonGroup,
    QStatusBar, QTableWidget, QTableWidgetItem, QHeaderView,
    QAbstractItemView, QMessageBox, QFileDialog, QFrame, QSizePolicy,
    QScrollArea, QSpinBox, QCheckBox, QTabWidget,
)

from protocol import (
    DisplayMode, FormatMode, KeyName, MsgDir, MsgType,
    BAUD_RATES, DEFAULT_BAUD,
    parse_message, seg7_pattern,
    parse_time_response, parse_alarm_response,
    parse_disp, parse_led, parse_weather_response,
)
from event_logger import EventLogger
from serial_worker import SerialWorker, scan_ports

# ── Colors ─────────────────────────────────────────────────────────
COLOR_SEG_ON = QColor(0, 255, 0, 220)
COLOR_SEG_OFF = QColor(20, 40, 20, 80)
COLOR_SEG_NIGHT_ON = QColor(180, 120, 20, 200)
COLOR_SEG_NIGHT_OFF = QColor(30, 20, 5, 50)
COLOR_LED_ON = QColor(255, 60, 40)
COLOR_LED_OFF = QColor(50, 20, 18)
COLOR_LED_HB = QColor(0, 200, 255)

COLOR_SEND = QColor(100, 180, 255)
COLOR_RECV = QColor(120, 255, 120)
COLOR_EVENT = QColor(255, 200, 80)
COLOR_ERROR_LOG = QColor(255, 80, 80)
COLOR_SYSTEM = QColor(160, 160, 160)

# ── Auto day/night mode ────────────────────────────────────────────
DEFAULT_LOCATION = LocationInfo("Shanghai", "China", "Asia/Shanghai",
                                latitude=31.0278, longitude=121.2664)
AUTO_CHECK_INTERVAL_MS = 30_000

# ── Theme definitions ──────────────────────────────────────────────
DARK_THEME = {
    "name": "dark",
    # Global
    "bg": "#1a1a24", "fg": "#ccc", "group_border": "#444", "group_title": "#aac",
    "input_bg": "#2a2a36", "input_fg": "#ddd", "input_border": "#555",
    "input_sel_bg": "#3a3a5a",
    "btn_bg": "#333", "btn_fg": "#ccc", "btn_border": "#555",
    "btn_hover": "#3a3a3a", "btn_pressed": "#2a2a2a",
    "radio_fg": "#bbb",
    "statusbar_bg": "#111", "statusbar_border": "#444",
    "splitter": "#333",
    # LogPanel table
    "table_bg": "#16161e", "table_alt": "#1e1e28", "table_fg": "#ccc",
    "table_grid": "#333", "table_header_bg": "#222", "table_header_fg": "#aaa",
    # LogPanel buttons
    "log_btn_bg": "#333", "log_btn_fg": "#ccc", "log_btn_hover": "#444",
    "log_btn_checked": "#3a5a3a",
    "log_send": "#64b4ff", "log_recv": "#78ff78", "log_event": "#ffc850",
    "log_error": "#ff5050", "log_system": "#a0a0a0",
    # Digital twin
    "seg_frame_bg": "#0a0a10", "seg_frame_border": "#333",
    "seg_on": "#dc00ff00", "seg_off": "#50142814",
    "seg_night_on": "#c8b47814", "seg_night_off": "#331e1405",
    "led_label_fg": "#888",
    "key_btn_bg": "#3a3a4a", "key_btn_fg": "#ddd", "key_btn_border": "#555",
    "key_btn_hover": "#4a4a5a", "key_btn_pressed": "#2a5a3a",
    "test_btn_bg": "#2a4a2a", "test_btn_fg": "#afa",
    "test_btn_hover": "#3a5a3a",
    "stop_btn_bg": "#4a2a2a", "stop_btn_fg": "#faa", "stop_btn_hover": "#5a3a3a",
    # ControlPanel special buttons
    "connect_bg": "#2a5a3a", "connect_fg": "#cfc", "connect_hover": "#3a6a4a",
    "disconnect_bg": "#5a3a3a", "disconnect_fg": "#fcc", "disconnect_hover": "#6a4a4a",
    "ntp_btn_bg": "#2a4a5a", "ntp_btn_fg": "#adf", "ntp_btn_hover": "#3a5a6a",
    "weather_btn_bg": "#2a5a4a", "weather_btn_fg": "#afd", "weather_btn_hover": "#3a6a4a",
    "rst_btn_bg": "#5a2020", "rst_btn_fg": "#faa",
    # Status labels
    "lbl_dim": "#888", "lbl_mid": "#aaa",
    "lbl_conn_on": "#6f6", "lbl_conn_off": "#f66",
    "lbl_alarm_on": "#f66", "lbl_mode_night": "#fb0",
    "lbl_hb_flash": "#0f0", "lbl_pong": "#8f8", "lbl_weather": "#8cf",
    # Tabs (right side)
    "tab_pane_bg": "#1a1a2e", "tab_bg": "#16213e", "tab_fg": "#888",
    "tab_selected_bg": "#1a1a2e", "tab_selected_fg": "#eee",
    "tab_selected_border": "#58a6ff", "tab_hover_bg": "#1f2b47", "tab_hover_fg": "#bbb",
    # Dashboard tabs
    "dash_tab_pane_bg": "#0d1117", "dash_tab_bg": "#161b22", "dash_tab_fg": "#888",
    "dash_tab_selected_bg": "#0d1117", "dash_tab_selected_fg": "#eee",
    "dash_tab_selected_border": "#3fb950", "dash_tab_hover_bg": "#1c2333", "dash_tab_hover_fg": "#bbb",
    # Dashboard charts
    "chart_bg": "#0d1117", "chart_fg": "#8b949e", "chart_grid": "#21262d",
    "chart_title": "#c9d1d9",
    "chart_red": "#f85149", "chart_green": "#3fb950", "chart_blue": "#58a6ff",
    "chart_amber": "#d29922", "chart_purple": "#bc8cff",
}

LIGHT_THEME = {
    "name": "light",
    # Global
    "bg": "#f5f5f5", "fg": "#333", "group_border": "#ccc", "group_title": "#444",
    "input_bg": "#fff", "input_fg": "#222", "input_border": "#bbb",
    "input_sel_bg": "#cce5ff",
    "btn_bg": "#e0e0e0", "btn_fg": "#222", "btn_border": "#bbb",
    "btn_hover": "#d0d0d0", "btn_pressed": "#c0c0c0",
    "radio_fg": "#444",
    "statusbar_bg": "#e8e8e8", "statusbar_border": "#ccc",
    "splitter": "#ccc",
    # LogPanel table
    "table_bg": "#fff", "table_alt": "#f0f4f8", "table_fg": "#222",
    "table_grid": "#ddd", "table_header_bg": "#e0e0e0", "table_header_fg": "#444",
    # LogPanel buttons
    "log_btn_bg": "#e0e0e0", "log_btn_fg": "#333", "log_btn_hover": "#d0d0d0",
    "log_btn_checked": "#c8e6c9",
    "log_send": "#1565c0", "log_recv": "#2e7d32", "log_event": "#e65100",
    "log_error": "#c62828", "log_system": "#555",
    # Digital twin
    "seg_frame_bg": "#e0e0e0", "seg_frame_border": "#bbb",
    "seg_on": "#ff008000", "seg_off": "#80c0c0c0",
    "seg_night_on": "#c8b47814", "seg_night_off": "#331e1405",
    "led_label_fg": "#666",
    "key_btn_bg": "#e8e8e8", "key_btn_fg": "#222", "key_btn_border": "#bbb",
    "key_btn_hover": "#d8d8d8", "key_btn_pressed": "#b0d8b0",
    "test_btn_bg": "#c8e6c9", "test_btn_fg": "#2e7d32",
    "test_btn_hover": "#a5d6a7",
    "stop_btn_bg": "#ffcdd2", "stop_btn_fg": "#c62828", "stop_btn_hover": "#ef9a9a",
    # ControlPanel special buttons
    "connect_bg": "#c8e6c9", "connect_fg": "#2e7d32", "connect_hover": "#a5d6a7",
    "disconnect_bg": "#ffcdd2", "disconnect_fg": "#c62828", "disconnect_hover": "#ef9a9a",
    "ntp_btn_bg": "#bbdefb", "ntp_btn_fg": "#1565c0", "ntp_btn_hover": "#90caf9",
    "weather_btn_bg": "#b2dfdb", "weather_btn_fg": "#00695c", "weather_btn_hover": "#80cbc4",
    "rst_btn_bg": "#ffcdd2", "rst_btn_fg": "#c62828",
    # Status labels
    "lbl_dim": "#888", "lbl_mid": "#555",
    "lbl_conn_on": "#2e7d32", "lbl_conn_off": "#c62828",
    "lbl_alarm_on": "#c62828", "lbl_mode_night": "#e65100",
    "lbl_hb_flash": "#2e7d32", "lbl_pong": "#2e7d32", "lbl_weather": "#1565c0",
    # Tabs (right side)
    "tab_pane_bg": "#fff", "tab_bg": "#e8e8e8", "tab_fg": "#666",
    "tab_selected_bg": "#fff", "tab_selected_fg": "#222",
    "tab_selected_border": "#1976d2", "tab_hover_bg": "#d8e8f8", "tab_hover_fg": "#333",
    # Dashboard tabs
    "dash_tab_pane_bg": "#fafafa", "dash_tab_bg": "#e8e8e8", "dash_tab_fg": "#666",
    "dash_tab_selected_bg": "#fafafa", "dash_tab_selected_fg": "#222",
    "dash_tab_selected_border": "#2e7d32", "dash_tab_hover_bg": "#d8ead8", "dash_tab_hover_fg": "#333",
    # Dashboard charts
    "chart_bg": "#fafafa", "chart_fg": "#555", "chart_grid": "#e0e0e0",
    "chart_title": "#222",
    "chart_red": "#d32f2f", "chart_green": "#388e3c", "chart_blue": "#1976d2",
    "chart_amber": "#f57c00", "chart_purple": "#7b1fa2",
}

_current_theme = DARK_THEME


def get_theme() -> dict:
    return _current_theme


def set_theme(theme: dict):
    global _current_theme
    _current_theme = theme


# ═══════════════════════════════════════════════════════════════════
# Seven‑Segment Widget
# ═══════════════════════════════════════════════════════════════════

class SevenSegmentWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._pattern = 0
        self._dp = False
        self._is_night = False
        self._visible_in_night = True
        self.setMinimumSize(24, 60)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

    def set_segments(self, pattern: int, dp: bool = False):
        self._pattern = pattern & 0x7F
        self._dp = dp
        self.update()

    def set_night_mode(self, night: bool, visible: bool = True):
        self._is_night = night
        self._visible_in_night = visible
        self.update()

    def paintEvent(self, event):
        if self._is_night and not self._visible_in_night:
            return

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()

        # Reserve right 15 % for decimal point
        avail_w = w * 0.82
        avail_h = h * 0.92

        # Enforce 7‑seg aspect:  width ≈ 0.56 × height
        target_w = avail_h * 0.56
        if avail_w > target_w:
            body_w = target_w
            body_h = avail_h
        else:
            body_w = avail_w
            body_h = body_w / 0.52

        # Centre in the left 85 % of the widget
        bx = (avail_w - body_w) / 2.0
        by = (h - body_h) / 2.0

        t = max(body_w * 0.15, 2.2)
        s = t * 0.40

        xl = bx + t * 0.15
        xr = bx + body_w - t * 0.15
        yt = by + t * 0.10
        ym = by + body_h / 2.0
        yb = by + body_h - t * 0.10

        theme = get_theme()
        if self._is_night:
            on = QColor(theme["seg_night_on"])
            off = QColor(theme["seg_night_off"])
        else:
            on = QColor(theme["seg_on"])
            off = QColor(theme["seg_off"])

        def hseg(y):
            return QPolygonF([
                QPointF(xl + s, y - t / 2), QPointF(xr - s, y - t / 2),
                QPointF(xr, y), QPointF(xr - s, y + t / 2),
                QPointF(xl + s, y + t / 2), QPointF(xl, y),
            ])
        def vseg(x, y1, y2):
            return QPolygonF([
                QPointF(x, y1), QPointF(x + t / 2, y1 + s),
                QPointF(x + t / 2, y2 - s), QPointF(x, y2),
                QPointF(x - t / 2, y2 - s), QPointF(x - t / 2, y1 + s),
            ])

        gap = t * 0.30
        segs = [
            hseg(yt),                          # 0: A
            vseg(xr, yt, ym - gap),            # 1: B
            vseg(xr, ym + gap, yb),            # 2: C
            hseg(yb),                          # 3: D
            vseg(xl, ym + gap, yb),            # 4: E
            vseg(xl, yt, ym - gap),            # 5: F
            hseg(ym),                          # 6: G
        ]

        for idx, poly in enumerate(segs):
            lit = bool(self._pattern & (1 << idx))
            color = on if lit else off
            painter.setBrush(QBrush(color))
            painter.setPen(Qt.NoPen)
            painter.drawPolygon(poly)

        # Decimal point — to the right of the digit body
        # (positioned so two adjacent dots form a colon between digits)
        if self._dp:
            dp_r = t * 0.55
            dp_cx = bx + body_w + dp_r * 1.6   # gap from body right edge
            dp_cy = yb + dp_r * 0.5             # slightly below the D segment
            dp_color = on if not self._is_night else COLOR_SEG_NIGHT_ON
            painter.setBrush(QBrush(dp_color))
            painter.setPen(Qt.NoPen)
            painter.drawEllipse(QPointF(dp_cx, dp_cy), dp_r, dp_r)

        painter.end()


# ═══════════════════════════════════════════════════════════════════
# LED Indicator
# ═══════════════════════════════════════════════════════════════════

class LedIndicator(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._lit = False
        self._color = COLOR_LED_ON
        self.setFixedSize(24, 24)

    def set_lit(self, lit: bool):
        self._lit = lit
        self.update()

    def pulse(self, duration_ms: int = 100):
        """Briefly light the LED for *duration_ms*, then turn off."""
        self.set_lit(True)
        QTimer.singleShot(duration_ms, lambda: self.set_lit(False))

    def set_color(self, color: QColor):
        self._color = color
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        cx, cy = self.width() / 2, self.height() / 2
        r = min(cx, cy) - 2
        if self._lit:
            glow = QColor(self._color.red(), self._color.green(),
                          self._color.blue(), 80)
            painter.setBrush(QBrush(glow))
            painter.setPen(Qt.NoPen)
            painter.drawEllipse(QPointF(cx, cy), r + 3, r + 3)
            painter.setBrush(QBrush(self._color))
            painter.drawEllipse(QPointF(cx, cy), r, r)
        else:
            painter.setBrush(QBrush(COLOR_LED_OFF))
            painter.setPen(QPen(QColor(40, 15, 15), 1))
            painter.drawEllipse(QPointF(cx, cy), r, r)
        painter.end()


# ═══════════════════════════════════════════════════════════════════
# Digital Twin Panel
# ═══════════════════════════════════════════════════════════════════

LED_LABELS = ["LED1", "LED2", "LED3", "LED4", "LED5", "LED6", "LED7", "LED8"]


class DigitalTwinPanel(QGroupBox):
    key_pressed = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__("数字孪生镜像", parent)
        self._display_mode = DisplayMode.DAY
        self._wx_rain_timer: Optional[QTimer] = None
        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)

        # ── 7‑segment display ──
        self._seg_frame = QFrame()
        self._seg_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        self._seg_frame.setStyleSheet(
            "QFrame{background:#0a0a10;border:1px solid #333;border-radius:4px;}")
        self._seg_frame.setMinimumHeight(110)
        seg_layout = QHBoxLayout(self._seg_frame)
        seg_layout.setContentsMargins(4, 2, 4, 2)
        seg_layout.setSpacing(0)
        self.seg_widgets = []
        for i in range(8):
            sw = SevenSegmentWidget()
            seg_layout.addWidget(sw)
            self.seg_widgets.append(sw)
        layout.addWidget(self._seg_frame)

        # ── LED row ──
        led_layout = QHBoxLayout()
        led_layout.setSpacing(8)
        led_layout.addStretch()
        self.led_widgets = []
        self._led_labels = []
        for i in range(8):
            led = LedIndicator()
            self.led_widgets.append(led)
            lbl = QLabel(LED_LABELS[i])
            lbl.setStyleSheet("color:#888;font-size:9px;")
            self._led_labels.append(lbl)
            lbl.setAlignment(Qt.AlignCenter)
            il = QVBoxLayout()
            il.setSpacing(1)
            il.addWidget(led, alignment=Qt.AlignCenter)
            il.addWidget(lbl, alignment=Qt.AlignCenter)
            led_layout.addLayout(il)
        led_layout.addStretch()
        layout.addLayout(led_layout)

        # ── Virtual keys ──
        key_grid = QGridLayout()
        key_grid.setSpacing(6)
        key_names = list(KeyName)
        self._key_buttons = []
        for i, kn in enumerate(key_names):
            btn = QPushButton(kn.value)
            self._key_buttons.append(btn)
            btn.setMinimumHeight(36)
            btn.setStyleSheet(
                "QPushButton{background:#3a3a4a;color:#ddd;border:1px solid #555;"
                "border-radius:4px;font-weight:bold;}"
                "QPushButton:hover{background:#4a4a5a;}"
                "QPushButton:pressed{background:#2a5a3a;}"
            )
            btn.clicked.connect(lambda checked, k=kn: self.key_pressed.emit(k.value))
            key_grid.addWidget(btn, i // 4, i % 4)
        layout.addLayout(key_grid)

        layout.addStretch()
        self.setMinimumWidth(340)

    # ── Mirror update ─────────────────────────────────────────────

    def apply_time(self, text8: str, dp_mask: int = 0):
        """Show time on the 7‑segment display, with colons as decimal points."""
        night = self._display_mode == DisplayMode.NIGHT
        for i in range(8):
            ch = text8[i] if i < len(text8) else ' '
            pat = seg7_pattern(ch)
            dp = bool(dp_mask & (1 << i))
            self.seg_widgets[i].set_segments(pat, dp)
            visible = not (night and i >= 4)
            self.seg_widgets[i].set_night_mode(night, visible)

    def apply_disp(self, text8: str, dp_mask: int = 0):
        """Generic display update (for self‑test)."""
        for i in range(8):
            ch = text8[i] if i < len(text8) else ' '
            dp = bool(dp_mask & (1 << i))
            pat = seg7_pattern(ch)
            self.seg_widgets[i].set_segments(pat, dp)
            self.seg_widgets[i].set_night_mode(False, True)

    def apply_led(self, mask: int):
        """Update LED states.  S800 active‑low: 0 = on, 1 = off."""
        for i in range(8):
            if i == 5 and self._wx_rain_timer and self._wx_rain_timer.isActive():
                continue  # client-side blink overrides snapshot
            self.led_widgets[i].set_lit(not bool(mask & (1 << i)))

    def apply_mode(self, mode: DisplayMode):
        self._display_mode = mode

    def pulse_heartbeat_led(self):
        """Briefly light LED1 (HB, bit 0)."""
        self.led_widgets[0].set_lit(True)
        QTimer.singleShot(200, lambda: self.led_widgets[0].set_lit(False))

    def set_alarm_led(self, on: bool):
        """LED2 (ALM, bit 1)."""
        self.led_widgets[1].set_lit(on)

    def pulse_uart_led(self):
        """Briefly light LED4 (UART, bit 3) — mirrors firmware 100ms TX/RX pulse."""
        self.led_widgets[3].pulse(100)

    def start_wx_rain_blink(self):
        """Start 1Hz blink on LED5 (WX_RAIN, bit 5) — mirrors firmware rain/snow blink."""
        if self._wx_rain_timer is None:
            self._wx_rain_timer = QTimer(self)
            self._wx_rain_timer.timeout.connect(self._toggle_wx_rain)
            self._wx_rain_blink_state = False
        if not self._wx_rain_timer.isActive():
            self._wx_rain_blink_state = False
            self._wx_rain_timer.start(500)

    def stop_wx_rain_blink(self):
        """Stop WX_RAIN blink and turn LED5 off."""
        if self._wx_rain_timer and self._wx_rain_timer.isActive():
            self._wx_rain_timer.stop()
        self.led_widgets[5].set_lit(False)

    def _toggle_wx_rain(self):
        self._wx_rain_blink_state = not self._wx_rain_blink_state
        self.led_widgets[5].set_lit(self._wx_rain_blink_state)



# ═══════════════════════════════════════════════════════════════════
# Control Panel
# ═══════════════════════════════════════════════════════════════════

class ControlPanel(QGroupBox):
    send_command = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__("控制面板", parent)
        self._init_ui()

    def _init_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 2, 0, 0)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setStyleSheet("QScrollArea{background:transparent;border:none;}")
        content = QWidget()
        layout = QVBoxLayout(content)
        layout.setSpacing(6)

        # ── Serial ──
        sg = QGroupBox("串口设置")
        sf = QFormLayout(sg)
        self.combo_port = QComboBox()
        self.combo_port.setEditable(False)
        self.combo_port.setSizeAdjustPolicy(QComboBox.AdjustToContents)
        self.combo_port.setMinimumContentsLength(16)
        self.btn_refresh = QPushButton("刷新")
        self.btn_refresh.clicked.connect(self._refresh_ports)
        pr = QHBoxLayout()
        pr.addWidget(self.combo_port, 1)
        pr.addWidget(self.btn_refresh, 0)
        sf.addRow("COM口:", pr)
        self.combo_baud = QComboBox()
        for b in BAUD_RATES:
            self.combo_baud.addItem(str(b), b)
        self.combo_baud.setCurrentText(str(DEFAULT_BAUD))
        self.combo_baud.setSizeAdjustPolicy(QComboBox.AdjustToContents)
        self.combo_baud.setMinimumContentsLength(7)
        sf.addRow("波特率:", self.combo_baud)
        br = QHBoxLayout()
        self.btn_connect = QPushButton("连接")
        self.btn_connect.setStyleSheet(
            "QPushButton{background:#2a5a3a;color:#cfc;font-weight:bold;"
            "border-radius:3px;padding:6px 16px;}"
            "QPushButton:hover{background:#3a6a4a;}")
        self.btn_disconnect = QPushButton("断开")
        self.btn_disconnect.setEnabled(False)
        self.btn_disconnect.setStyleSheet(
            "QPushButton{background:#5a3a3a;color:#fcc;font-weight:bold;"
            "border-radius:3px;padding:6px 16px;}"
            "QPushButton:hover{background:#6a4a4a;}")
        br.addWidget(self.btn_connect)
        br.addWidget(self.btn_disconnect)
        sf.addRow(br)
        layout.addWidget(sg)

        # ── System (moved here from bottom) ──
        sys_g = QGroupBox("系统")
        sys_l = QHBoxLayout(sys_g)
        self.btn_ntp = QPushButton("NTP对时")
        self.btn_ntp.setStyleSheet(
            "QPushButton{background:#2a4a5a;color:#adf;font-weight:bold;"
            "border-radius:3px;padding:6px 14px;}"
            "QPushButton:hover{background:#3a5a6a;}")
        self.btn_ntp.setToolTip("从NTP服务器获取标准时间并下发到S800板")
        sys_l.addWidget(self.btn_ntp)
        self.btn_weather = QPushButton("天气获取")
        self.btn_weather.setStyleSheet(
            "QPushButton{background:#2a5a4a;color:#afd;font-weight:bold;"
            "border-radius:3px;padding:6px 14px;}"
            "QPushButton:hover{background:#3a6a4a;}")
        self.btn_weather.setToolTip("获取本地天气（温度+天气状况）并下发到S800板")
        sys_l.addWidget(self.btn_weather)
        self.btn_rst = QPushButton("*RST 复位")
        self.btn_rst.setStyleSheet("QPushButton{background:#5a2020;color:#faa;font-weight:bold;}")
        self.btn_rst.clicked.connect(lambda: self.send_command.emit("*RST"))
        btn_ping = QPushButton("*PING")
        btn_ping.clicked.connect(lambda: self.send_command.emit("*PING"))
        sys_l.addWidget(self.btn_rst)
        sys_l.addWidget(btn_ping)
        self.chk_voice = QCheckBox("语音播报")
        sys_l.addWidget(self.chk_voice)
        self.combo_voice = QComboBox()
        for name, _ in VoiceAnnouncer.VOICES:
            self.combo_voice.addItem(name)
        self.combo_voice.setCurrentIndex(0)
        self.combo_voice.setMinimumWidth(180)
        sys_l.addWidget(self.combo_voice)
        sys_l.addStretch()
        layout.addWidget(sys_g)

        # ── Time ──
        tg = QGroupBox("时间设置 (*SET:TIME)")
        tf = QFormLayout(tg)
        r1 = QHBoxLayout()
        self.spin_hour = QSpinBox(); self.spin_hour.setRange(0, 23); self.spin_hour.setValue(12)
        self.spin_min = QSpinBox(); self.spin_min.setRange(0, 59); self.spin_min.setValue(0)
        self.spin_sec = QSpinBox(); self.spin_sec.setRange(0, 59); self.spin_sec.setValue(0)
        for sb, lbl in [(self.spin_hour, "时"), (self.spin_min, "分"), (self.spin_sec, "秒")]:
            r1.addWidget(QLabel(lbl))
            r1.addWidget(sb)
        r1.addStretch()
        btn_time = QPushButton("设置时间")
        btn_time.clicked.connect(lambda: self.send_command.emit(
            f"*SET:TIME HOUR {self.spin_hour.value()} MIN {self.spin_min.value()} SEC {self.spin_sec.value()}"))
        r1.addWidget(btn_time)
        tf.addRow(r1)
        layout.addWidget(tg)

        # ── Date ──
        dg = QGroupBox("日期设置 (*SET:DATE)")
        df = QFormLayout(dg)
        r2 = QHBoxLayout()
        self.spin_year = QSpinBox(); self.spin_year.setRange(2000, 2099)
        self.spin_year.setValue(datetime.now().year)
        self.spin_month = QSpinBox(); self.spin_month.setRange(1, 12)
        self.spin_month.setValue(datetime.now().month)
        self.spin_day = QSpinBox(); self.spin_day.setRange(1, 31)
        self.spin_day.setValue(datetime.now().day)
        for sb, lbl in [(self.spin_year, "年"), (self.spin_month, "月"), (self.spin_day, "日")]:
            r2.addWidget(QLabel(lbl))
            r2.addWidget(sb)
        r2.addStretch()
        btn_date = QPushButton("设置日期")
        btn_date.clicked.connect(lambda: self.send_command.emit(
            f"*SET:DATE YEAR {self.spin_year.value()} MONTH {self.spin_month.value()} DATE {self.spin_day.value()}"))
        r2.addWidget(btn_date)
        df.addRow(r2)
        layout.addWidget(dg)

        # ── Alarm ──
        ag = QGroupBox("闹钟设置 (*SET:ALARM)")
        af = QFormLayout(ag)
        r3 = QHBoxLayout()
        self.spin_ah = QSpinBox(); self.spin_ah.setRange(0, 23); self.spin_ah.setValue(7)
        self.spin_am = QSpinBox(); self.spin_am.setRange(0, 59); self.spin_am.setValue(0)
        self.spin_as = QSpinBox(); self.spin_as.setRange(0, 59); self.spin_as.setValue(0)
        for sb, lbl in [(self.spin_ah, "时"), (self.spin_am, "分"), (self.spin_as, "秒")]:
            r3.addWidget(QLabel(lbl))
            r3.addWidget(sb)
        r3.addStretch()
        btn_alarm = QPushButton("启用闹钟")
        btn_alarm.clicked.connect(lambda: self.send_command.emit(
            f"*SET:ALARM HOUR {self.spin_ah.value()} MIN {self.spin_am.value()} SEC {self.spin_as.value()}"))
        r3.addWidget(btn_alarm)
        btn_alarm_off = QPushButton("关闭闹钟")
        btn_alarm_off.clicked.connect(lambda: self.send_command.emit("*SET:ALARM OFF"))
        r3.addWidget(btn_alarm_off)
        af.addRow(r3)
        layout.addWidget(ag)

        # ── Display / Format / Mode ──
        dg2 = QGroupBox("显示控制")
        df2 = QFormLayout(dg2)

        # Display ON/OFF
        dr = QHBoxLayout()
        self.btn_disp_on = QRadioButton("ON"); self.btn_disp_on.setChecked(True)
        self.btn_disp_off = QRadioButton("OFF")
        dg_grp = QButtonGroup(self); dg_grp.addButton(self.btn_disp_on); dg_grp.addButton(self.btn_disp_off)
        dr.addWidget(QLabel("DISPLAY:"))
        dr.addWidget(self.btn_disp_on); dr.addWidget(self.btn_disp_off)
        dr.addStretch()
        btn_do = QPushButton("↩"); btn_do.setFixedWidth(36)
        btn_do.clicked.connect(lambda: self.send_command.emit(
            "*SET:DISPLAY ON" if self.btn_disp_on.isChecked() else "*SET:DISPLAY OFF"))
        dr.addWidget(btn_do)
        df2.addRow(dr)

        # Format LEFT/RIGHT
        fr = QHBoxLayout()
        self.btn_fmt_l = QRadioButton("LEFT"); self.btn_fmt_l.setChecked(True)
        self.btn_fmt_r = QRadioButton("RIGHT")
        fg = QButtonGroup(self); fg.addButton(self.btn_fmt_l); fg.addButton(self.btn_fmt_r)
        fr.addWidget(QLabel("FORMAT:"))
        fr.addWidget(self.btn_fmt_l); fr.addWidget(self.btn_fmt_r)
        fr.addStretch()
        btn_f = QPushButton("↩"); btn_f.setFixedWidth(36)
        btn_f.clicked.connect(lambda: self.send_command.emit(
            "*SET:FORMAT LEFT" if self.btn_fmt_l.isChecked() else "*SET:FORMAT RIGHT"))
        fr.addWidget(btn_f)
        df2.addRow(fr)

        # Mode DAY/NIGHT
        mr = QHBoxLayout()
        self.btn_md = QRadioButton("DAY"); self.btn_md.setChecked(True)
        self.btn_mn = QRadioButton("NIGHT")
        mg = QButtonGroup(self); mg.addButton(self.btn_md); mg.addButton(self.btn_mn)
        mr.addWidget(QLabel("MODE:"))
        mr.addWidget(self.btn_md); mr.addWidget(self.btn_mn)
        mr.addStretch()
        btn_m = QPushButton("↩"); btn_m.setFixedWidth(36)
        btn_m.clicked.connect(lambda: self.send_command.emit(
            "*SET:MODE DAY" if self.btn_md.isChecked() else "*SET:MODE NIGHT"))
        mr.addWidget(btn_m)
        df2.addRow(mr)
        # Auto day/night switch
        ar = QHBoxLayout()
        self.chk_auto_mode = QCheckBox("自动日夜切换")
        self.chk_auto_mode.setChecked(True)
        ar.addWidget(self.chk_auto_mode)
        ar.addStretch()
        df2.addRow(ar)
        # Sunrise/sunset info
        sr = QHBoxLayout()
        self.lbl_sunrise = QLabel("日出: --:--")
        self.lbl_sunrise.setStyleSheet("color:#888;")
        self.lbl_sunset = QLabel("日落: --:--")
        self.lbl_sunset.setStyleSheet("color:#888;")
        sr.addWidget(self.lbl_sunrise)
        sr.addWidget(self.lbl_sunset)
        sr.addStretch()
        df2.addRow(sr)
        layout.addWidget(dg2)

        # ── Message ──
        msg_g = QGroupBox("消息滚动 (*SET:MSG)")
        msg_l = QHBoxLayout(msg_g)
        self.edit_msg = QLineEdit()
        self.edit_msg.setPlaceholderText("≤32字节，如 HELLO")
        self.edit_msg.returnPressed.connect(self._send_msg)
        btn_msg = QPushButton("发送")
        btn_msg.clicked.connect(self._send_msg)
        msg_l.addWidget(self.edit_msg, 1)
        msg_l.addWidget(btn_msg, 0)
        layout.addWidget(msg_g)

        # ── Beep / LED ──
        blg = QGroupBox("蜂鸣 / LED")
        blf = QFormLayout(blg)
        br2 = QHBoxLayout()
        self.spin_beep = QSpinBox(); self.spin_beep.setRange(10, 5000); self.spin_beep.setValue(200)
        br2.addWidget(QLabel("蜂鸣 ms:"))
        br2.addWidget(self.spin_beep)
        btn_beep = QPushButton("蜂鸣")
        btn_beep.clicked.connect(lambda: self.send_command.emit(f"*SET:BEEP {self.spin_beep.value()}"))
        br2.addWidget(btn_beep)
        br2.addStretch()
        self.edit_led = QLineEdit(); self.edit_led.setPlaceholderText("LED hex, 如 FF")
        self.edit_led.setMaximumWidth(60)
        br2.addWidget(QLabel("LED:"))
        br2.addWidget(self.edit_led)
        btn_led = QPushButton("发送")
        btn_led.clicked.connect(lambda: self.send_command.emit(f"*SET:LED {self.edit_led.text().strip()}"))
        br2.addWidget(btn_led)
        blf.addRow(br2)
        layout.addWidget(blg)


        # ── Raw command ──
        raw_g = QGroupBox("原始命令")
        rv2 = QHBoxLayout(raw_g)
        self.edit_raw = QLineEdit()
        self.edit_raw.setPlaceholderText("输入命令，如 *SET:MSG HELLO")
        self.edit_raw.returnPressed.connect(self._send_raw)
        btn_send = QPushButton("发送")
        btn_send.clicked.connect(self._send_raw)
        rv2.addWidget(self.edit_raw, 1)
        rv2.addWidget(btn_send, 0)
        layout.addWidget(raw_g)

        scroll.setWidget(content)
        outer.addWidget(scroll)

    def _refresh_ports(self):
        current = self.combo_port.currentData()
        self.combo_port.clear()
        for p in scan_ports():
            self.combo_port.addItem(f"{p['port']} — {p['description']}", p["port"])
        if current:
            idx = self.combo_port.findData(current)
            if idx >= 0:
                self.combo_port.setCurrentIndex(idx)

    def _send_raw(self):
        txt = self.edit_raw.text().strip()
        if txt:
            self.send_command.emit(txt)
            self.edit_raw.clear()

    def _send_msg(self):
        txt = self.edit_msg.text().strip()
        if txt:
            self.send_command.emit(f"*SET:MSG {txt}")
            self.edit_msg.clear()

    def get_current_port(self) -> Optional[str]:
        return self.combo_port.currentData()

    def get_current_baud(self) -> int:
        return self.combo_baud.currentData() or DEFAULT_BAUD


# ═══════════════════════════════════════════════════════════════════
# Voice Announcer
# ═══════════════════════════════════════════════════════════════════

class VoiceAnnouncer:
    """后台线程语音播报（Windows OneCore TTS）。"""

    VOICES = [
        ("慧慧（中文女声）", 0),
        ("瑶瑶（中文女声）", 1),
        ("康康（中文男声）", 2),
    ]

    def __init__(self):
        self._enabled = False
        self._voice_index = 0
        self._lock = threading.Lock()

    def set_enabled(self, on: bool):
        self._enabled = on

    def set_voice(self, index: int):
        if 0 <= index < len(self.VOICES):
            self._voice_index = index

    def speak(self, text: str):
        if not self._enabled:
            return
        threading.Thread(target=self._speak_thread, args=(text,), daemon=True).start()

    def _speak_thread(self, text):
        with self._lock:
            try:
                import asyncio
                asyncio.run(self._async_speak(text))
            except Exception as e:
                print(f"[VoiceAnnouncer] Error: {e}")

    async def _async_speak(self, text):
        from winrt.windows.media.speechsynthesis import SpeechSynthesizer
        from winrt.windows.storage.streams import DataReader
        synth = SpeechSynthesizer()
        voices = SpeechSynthesizer.all_voices
        synth.voice = voices[self._voice_index]
        stream = await synth.synthesize_text_to_stream_async(text)
        reader = DataReader(stream.get_input_stream_at(0))
        await reader.load_async(stream.size)
        data = bytes(reader.read_buffer(stream.size))
        import tempfile, os, winsound
        with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as f:
            f.write(data)
            path = f.name
        winsound.PlaySound(path, winsound.SND_FILENAME)
        os.unlink(path)


# ═══════════════════════════════════════════════════════════════════
# Log Panel
# ═══════════════════════════════════════════════════════════════════

class LogPanel(QGroupBox):
    COLORS = {
        MsgDir.SEND: QColor(100, 180, 255),
        MsgDir.RECV: QColor(120, 255, 120),
        MsgDir.EVENT: QColor(255, 200, 80),
        MsgDir.ERROR: QColor(255, 80, 80),
        MsgDir.SYSTEM: QColor(160, 160, 160),
    }

    def __init__(self, parent=None):
        super().__init__("收发日志", parent)
        self._init_ui()

    def update_theme(self, theme: dict):
        self.COLORS = {
            MsgDir.SEND: QColor(theme["log_send"]),
            MsgDir.RECV: QColor(theme["log_recv"]),
            MsgDir.EVENT: QColor(theme["log_event"]),
            MsgDir.ERROR: QColor(theme["log_error"]),
            MsgDir.SYSTEM: QColor(theme["log_system"]),
        }
        # Recolor existing rows
        for row in range(self.table.rowCount()):
            dir_item = self.table.item(row, 1)
            if dir_item:
                try:
                    d = MsgDir(dir_item.text())
                    c = self.COLORS.get(d, QColor(theme["log_system"]))
                except ValueError:
                    c = QColor(theme["log_system"])
                for col in range(self.table.columnCount()):
                    item = self.table.item(row, col)
                    if item:
                        item.setForeground(c)

    def _init_ui(self):
        layout = QVBoxLayout(self)
        self.table = QTableWidget(0, 3)
        self.table.setHorizontalHeaderLabels(["时间", "方向", "消息"])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.Stretch)
        self.table.verticalHeader().setVisible(False)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.table.setStyleSheet(
            "QTableWidget{alternate-background-color:#1e1e28;background:#16161e;"
            "color:#ccc;gridline-color:#333;}"
            "QHeaderView::section{background:#222;color:#aaa;border:1px solid #333;}")
        layout.addWidget(self.table)
        br = QHBoxLayout()
        self.btn_export = QPushButton("导出日志")
        self.btn_export.clicked.connect(self._export)
        self.btn_clear = QPushButton("清空")
        self.btn_clear.clicked.connect(self._clear)
        self.chk_auto = QPushButton("自动滚动")
        self.chk_auto.setCheckable(True); self.chk_auto.setChecked(True)
        for b in [self.btn_export, self.btn_clear, self.chk_auto]:
            b.setStyleSheet(
                "QPushButton{background:#333;color:#ccc;padding:4px 10px;border-radius:3px;}"
                "QPushButton:hover{background:#444;}"
                "QPushButton:checked{background:#3a5a3a;}")
            br.addWidget(b)
        br.addStretch()
        layout.addLayout(br)

    def add_log(self, direction: MsgDir, message: str):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        row = self.table.rowCount()
        self.table.insertRow(row)
        c = self.COLORS.get(direction, COLOR_SYSTEM)
        for col, text in enumerate([ts, direction.value, message]):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            item.setFont(QFont("Consolas", 9))
            self.table.setItem(row, col, item)
        if self.chk_auto.isChecked():
            self.table.scrollToBottom()

    def _export(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "导出日志", "s800_log.txt",
            "Text Files (*.txt);;CSV (*.csv);;All Files (*)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                sep = "," if path.endswith(".csv") else "\t"
                f.write(sep.join(["时间", "方向", "消息"]) + "\n")
                for r in range(self.table.rowCount()):
                    t = self.table.item(r, 0).text()
                    d = self.table.item(r, 1).text()
                    m = self.table.item(r, 2).text()
                    f.write(sep.join([t, d, m]) + "\n")
            self.add_log(MsgDir.SYSTEM, f"日志已导出: {path}")
        except OSError as exc:
            QMessageBox.critical(self, "导出失败", str(exc))

    def _clear(self):
        self.table.setRowCount(0)


# ═══════════════════════════════════════════════════════════════════
# Dashboard Panel (charts)
# ═══════════════════════════════════════════════════════════════════

from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg
from matplotlib.figure import Figure
from collections import Counter


class DashboardPanel(QGroupBox):
    """Data visualization dashboard with matplotlib charts."""

    def __init__(self, event_logger: EventLogger, parent=None):
        super().__init__("数据看板", parent)
        self._logger = event_logger
        self._theme = get_theme()
        self._init_ui()

    def set_theme(self, theme: dict):
        self._theme = theme
        self._apply_tab_style()
        self.refresh()

    def _apply_tab_style(self):
        t = self._theme
        self._tabs.setStyleSheet(f"""
            QTabWidget::pane {{ border: 1px solid {t['group_border']}; background: {t['dash_tab_pane_bg']}; }}
            QTabBar::tab {{
                background: {t['dash_tab_bg']}; color: {t['dash_tab_fg']};
                padding: 4px 12px; border: 1px solid {t['group_border']};
                border-bottom: none; border-top-left-radius: 3px;
                border-top-right-radius: 3px; margin-right: 1px;
            }}
            QTabBar::tab:selected {{
                background: {t['dash_tab_selected_bg']}; color: {t['dash_tab_selected_fg']};
                border-bottom: 2px solid {t['dash_tab_selected_border']};
            }}
            QTabBar::tab:hover:!selected {{
                background: {t['dash_tab_hover_bg']}; color: {t['dash_tab_hover_fg']};
            }}
        """)

    def _style_ax(self, ax, fig):
        t = self._theme
        fig.patch.set_facecolor(t["chart_bg"])
        ax.set_facecolor(t["chart_bg"])
        ax.tick_params(colors=t["chart_fg"], labelsize=8)
        ax.xaxis.label.set_color(t["chart_fg"])
        ax.yaxis.label.set_color(t["chart_fg"])
        ax.title.set_color(t["chart_title"])
        for spine in ax.spines.values():
            spine.set_color(t["chart_grid"])
        ax.grid(True, color=t["chart_grid"], linewidth=0.5, alpha=0.6)

    def _empty_text(self, ax):
        ax.text(0.5, 0.5, "暂无数据", ha="center", va="center",
                fontsize=14, color=self._theme["chart_fg"],
                transform=ax.transAxes)

    def _init_ui(self):
        layout = QVBoxLayout(self)

        # Refresh button row
        br = QHBoxLayout()
        btn_refresh = QPushButton("刷新图表")
        btn_refresh.clicked.connect(self.refresh)
        br.addWidget(btn_refresh)
        btn_clear = QPushButton("清除数据")
        btn_clear.clicked.connect(self.clear_data)
        br.addWidget(btn_clear)
        br.addStretch()
        layout.addLayout(br)

        # Tab widget for charts
        self._tabs = QTabWidget()
        self._fig_alarm = Figure(figsize=(5, 3), dpi=100)
        self._fig_ntp = Figure(figsize=(5, 3), dpi=100)
        self._fig_key = Figure(figsize=(5, 3), dpi=100)

        self._tabs.addTab(FigureCanvasQTAgg(self._fig_alarm), "闹钟分布")
        self._tabs.addTab(FigureCanvasQTAgg(self._fig_ntp), "NTP精度")
        self._tabs.addTab(FigureCanvasQTAgg(self._fig_key), "按键热度")
        self._apply_tab_style()
        layout.addWidget(self._tabs)

    def refresh(self):
        """Reload CSV data and redraw all charts."""
        data = self._logger.load()
        self._draw_alarm_chart(data)
        self._draw_ntp_chart(data)
        self._draw_key_chart(data)

    def clear_data(self):
        """Clear events.csv and refresh charts."""
        import os
        try:
            path = self._logger.path
            if os.path.exists(path):
                with open(path, "w", newline="", encoding="utf-8-sig") as f:
                    import csv
                    csv.writer(f).writerow(EventLogger.COLUMNS)
            self._logger._inited = True
        except OSError:
            pass
        self.refresh()

    def _draw_alarm_chart(self, data: list):
        self._fig_alarm.clear()
        ax = self._fig_alarm.add_subplot(111)
        self._style_ax(ax, self._fig_alarm)
        alarm_hours = [
            int(row["timestamp"][11:13])
            for row in data
            if row.get("event_type") == "ALARM_TRIGGER"
        ]
        if not alarm_hours:
            self._empty_text(ax)
            ax.set_title("闹钟触发分布 (按小时)")
            self._fig_alarm.canvas.draw_idle()
            return
        counts = Counter(alarm_hours)
        hours = list(range(24))
        vals = [counts.get(h, 0) for h in hours]
        ax.bar(hours, vals, color=self._theme["chart_red"], alpha=0.85, width=0.7)
        ax.set_xlabel("小时")
        ax.set_ylabel("触发次数")
        ax.set_title("闹钟触发分布 (按小时)")
        ax.set_xticks(hours)
        ax.set_xticklabels([f"{h:02d}" for h in hours], fontsize=7)
        self._fig_alarm.tight_layout()
        self._fig_alarm.canvas.draw_idle()

    def _draw_ntp_chart(self, data: list):
        self._fig_ntp.clear()
        ax = self._fig_ntp.add_subplot(111)
        self._style_ax(ax, self._fig_ntp)
        sync_events = [
            (row["timestamp"], float(row["value"]))
            for row in data
            if row.get("event_type") == "SYNC"
            and row.get("value", "").replace(".", "").replace("-", "").isdigit()
        ]
        if not sync_events:
            self._empty_text(ax)
            ax.set_title("NTP同步精度")
            self._fig_ntp.canvas.draw_idle()
            return
        times = [ts[11:19] for ts, _ in sync_events]
        deltas = [d for _, d in sync_events]
        ax.fill_between(range(len(deltas)), deltas, alpha=0.3,
                        color=self._theme["chart_green"])
        ax.plot(range(len(deltas)), deltas, "o-",
                color=self._theme["chart_green"], markersize=4, linewidth=1.2)
        ax.axhline(y=1.0, color=self._theme["chart_red"], linestyle="--",
                    alpha=0.6, label="1.0s阈值")
        ax.set_xlabel("同步序号")
        ax.set_ylabel("偏差 (秒)")
        ax.set_title("NTP同步精度")
        ax.legend(fontsize=8, facecolor=self._theme["chart_bg"],
                  edgecolor=self._theme["chart_grid"],
                  labelcolor=self._theme["chart_fg"])
        if len(times) <= 10:
            ax.set_xticks(range(len(times)))
            ax.set_xticklabels(times, fontsize=7, rotation=30)
        self._fig_ntp.tight_layout()
        self._fig_ntp.canvas.draw_idle()

    def _draw_key_chart(self, data: list):
        self._fig_key.clear()
        ax = self._fig_key.add_subplot(111)
        self._style_ax(ax, self._fig_key)
        key_names = [
            row["value"] for row in data
            if row.get("event_type") == "KEY" and row.get("value")
        ]
        if not key_names:
            self._empty_text(ax)
            ax.set_title("按键热度图")
            self._fig_key.canvas.draw_idle()
            return
        counts = Counter(key_names)
        names = sorted(counts.keys())
        vals = [counts[n] for n in names]
        t = self._theme
        palette = [t["chart_blue"], t["chart_green"], t["chart_amber"],
                   t["chart_red"], t["chart_purple"],
                   "#39d2c0", "#db61a2", "#8b949e", "#f0883e", "#a5d6ff"]
        colors = [palette[i % len(palette)] for i in range(len(names))]
        ax.bar(names, vals, color=colors, alpha=0.85, width=0.6)
        ax.set_xlabel("按键")
        ax.set_ylabel("按下次数")
        ax.set_title("按键热度图")
        for i, v in enumerate(vals):
            ax.text(i, v + 0.1, str(v), ha="center", fontsize=8,
                    color=self._theme["chart_fg"])
        self._fig_key.tight_layout()
        self._fig_key.canvas.draw_idle()


# ═══════════════════════════════════════════════════════════════════
# Main Window
# ═══════════════════════════════════════════════════════════════════

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("S800 PC Host v2.0")
        self.resize(1280, 780)
        self.setMinimumSize(1024, 600)

        self._format = FormatMode.LEFT
        self._mode = DisplayMode.DAY
        self._alarm_enabled = False
        self._display_on = True
        self._last_time_text = "        "
        self._pong_count = 0

        # Auto day/night mode
        self._last_auto_mode: Optional[DisplayMode] = None
        self._sunrise_time: Optional[datetime] = None
        self._sunset_time: Optional[datetime] = None
        self._event_logger = EventLogger()
        self._announcer = VoiceAnnouncer()
        self._dashboard_visible = False
        self._pending_set: Optional[str] = None  # "TIME"/"DATE"/"ALARM" awaiting OK

        # ── Serial worker ──
        self._worker = SerialWorker()
        self._thread = QThread()
        self._worker.moveToThread(self._thread)
        self._thread.started.connect(self._worker.run_loop)
        self._worker.line_received.connect(self._on_line_received)
        self._worker.error_occurred.connect(self._on_error)
        self._worker.connection_changed.connect(self._on_connection_changed)
        self._worker.pong_received.connect(self._on_pong)

        self._init_ui()
        self._connect_signals()

        # UART LED3 pulse on TX/RX activity
        self._worker.tx_activity.connect(self._twin.pulse_uart_led)
        self._worker.line_received.connect(self._twin.pulse_uart_led)
        self._thread.start()

        # Port scan
        self._scan_timer = QTimer(self)
        self._scan_timer.timeout.connect(self._refresh_ports_auto)
        self._scan_timer.start(2000)
        self._refresh_ports_auto()

        # Auto mode check timer
        self._auto_mode_timer = QTimer(self)
        self._auto_mode_timer.timeout.connect(self._check_auto_mode)
        self._auto_mode_timer.start(AUTO_CHECK_INTERVAL_MS)
        self._check_auto_mode()

        # Dashboard auto-refresh timer (every 10s when tab is visible)
        self._dashboard_timer = QTimer(self)
        self._dashboard_timer.timeout.connect(self._auto_refresh_dashboard)
        self._dashboard_timer.start(10_000)

        # Auto NTP sync timer (every 10 minutes when connected)
        self._ntp_timer = QTimer(self)
        self._ntp_timer.timeout.connect(self._auto_ntp_sync)
        self._ntp_timer.start(10_000)

        self._log_panel.add_log(MsgDir.SYSTEM, "S800 PC Host v2.0 启动完成")

    def _init_ui(self):
        # Menu bar with theme toggle button
        menubar = self.menuBar()
        self._btn_theme = QPushButton("☀ 浅色模式")
        self._btn_theme.setStyleSheet(
            "QPushButton{background:transparent;color:#aaa;border:none;"
            "padding:4px 12px;font-size:12px;}"
            "QPushButton:hover{background:#333;border-radius:3px;}")
        self._btn_theme.clicked.connect(self._toggle_theme)
        menubar.setCornerWidget(self._btn_theme)

        cw = QWidget()
        self.setCentralWidget(cw)
        root = QHBoxLayout(cw)
        root.setContentsMargins(6, 6, 6, 6)

        # Left side: digital twin (top) + control panel (bottom)
        left_splitter = QSplitter(Qt.Vertical)
        self._twin = DigitalTwinPanel()
        left_splitter.addWidget(self._twin)
        self._control = ControlPanel()
        left_splitter.addWidget(self._control)
        left_splitter.setStretchFactor(0, 1)
        left_splitter.setStretchFactor(1, 1)

        # Main splitter: left | right (tabs: log + dashboard)
        main_splitter = QSplitter(Qt.Horizontal)
        main_splitter.addWidget(left_splitter)
        self._log_panel = LogPanel()
        self._dashboard = DashboardPanel(self._event_logger)
        self._right_tabs = QTabWidget()
        self._right_tabs.addTab(self._log_panel, "收发日志")
        self._right_tabs.addTab(self._dashboard, "数据看板")
        self._right_tabs.tabBarClicked.connect(self._on_right_tab_clicked)
        self._right_tabs.currentChanged.connect(self._on_right_tab_clicked)
        main_splitter.addWidget(self._right_tabs)
        main_splitter.setStretchFactor(0, 3)
        main_splitter.setStretchFactor(1, 2)
        main_splitter.setSizes([700, 500])
        root.addWidget(main_splitter)

        # Status bar
        sb = QStatusBar()
        self.setStatusBar(sb)
        self._lbl_conn = QLabel("○ 未连接")
        self._lbl_conn.setStyleSheet("color:#f66;font-weight:bold;padding:0 8px;")
        sb.addPermanentWidget(self._lbl_conn)
        self._lbl_format = QLabel("FORMAT:LEFT")
        self._lbl_format.setStyleSheet("color:#aaa;padding:0 8px;")
        sb.addPermanentWidget(self._lbl_format)
        self._lbl_mode = QLabel("MODE:DAY")
        self._lbl_mode.setStyleSheet("color:#aaa;padding:0 8px;")
        sb.addPermanentWidget(self._lbl_mode)
        self._lbl_alarm = QLabel("ALARM:OFF")
        self._lbl_alarm.setStyleSheet("color:#aaa;padding:0 8px;")
        sb.addPermanentWidget(self._lbl_alarm)
        self._lbl_disp = QLabel("DISP:ON")
        self._lbl_disp.setStyleSheet("color:#aaa;padding:0 8px;")
        sb.addPermanentWidget(self._lbl_disp)
        self._lbl_hb = QLabel("♡")
        self._lbl_hb.setStyleSheet("color:#888;font-size:14px;padding:0 8px;")
        sb.addPermanentWidget(self._lbl_hb)

        self._apply_theme()

    def _connect_signals(self):
        cp = self._control
        cp.btn_connect.clicked.connect(self._on_connect)
        cp.btn_disconnect.clicked.connect(self._on_disconnect)
        cp.send_command.connect(self._send_command)
        self._twin.key_pressed.connect(self._on_virtual_key_pressed)
        cp.btn_ntp.clicked.connect(self._ntp_sync)
        cp.btn_weather.clicked.connect(self._fetch_weather)
        cp.chk_auto_mode.stateChanged.connect(self._on_auto_mode_toggled)
        cp.chk_voice.stateChanged.connect(self._on_voice_toggled)
        cp.combo_voice.currentIndexChanged.connect(self._on_voice_changed)

    # ── Connection ─────────────────────────────────────────────────

    def _on_connect(self):
        port = self._control.get_current_port()
        baud = self._control.get_current_baud()
        if not port:
            QMessageBox.warning(self, "提示", "请先选择一个 COM 端口")
            return
        self._log_panel.add_log(MsgDir.SYSTEM, f"正在连接 {port} @ {baud}...")
        ok = self._worker.open_port(port, baud)
        if ok:
            self._log_panel.add_log(MsgDir.SYSTEM, f"已连接到 {port}")
            self._announcer.speak(f"已连接到 {port}")
        else:
            self._log_panel.add_log(MsgDir.ERROR, f"连接 {port} 失败，请检查端口是否被占用")
            QMessageBox.critical(self, "连接失败",
                                 f"无法打开 {port}\n请确认该端口未被其他程序占用")
            self._announcer.speak("连接失败")

    def _on_disconnect(self):
        self._worker.close_port()
        self._log_panel.add_log(MsgDir.SYSTEM, "已断开连接")
        self._announcer.speak("已断开连接")

    def _on_connection_changed(self, connected: bool, port: str):
        cp = self._control
        t = get_theme()
        self._event_logger.log("CONNECT", "ON" if connected else "OFF")
        if connected:
            self._lbl_conn.setText(f"● {port} 已连接")
            self._lbl_conn.setStyleSheet(f"color:{t['lbl_conn_on']};font-weight:bold;padding:0 8px;")
            cp.btn_connect.setEnabled(False); cp.btn_disconnect.setEnabled(True)
            cp.combo_port.setEnabled(False); cp.combo_baud.setEnabled(False)
            cp.btn_refresh.setEnabled(False)
        else:
            self._lbl_conn.setText("○ 未连接")
            self._lbl_conn.setStyleSheet(f"color:{t['lbl_conn_off']};font-weight:bold;padding:0 8px;")
            cp.btn_connect.setEnabled(True); cp.btn_disconnect.setEnabled(False)
            cp.combo_port.setEnabled(True); cp.combo_baud.setEnabled(True)
            cp.btn_refresh.setEnabled(True)
            self._lbl_hb.setText("♡")
            self._lbl_hb.setStyleSheet(f"color:{t['lbl_dim']};font-size:14px;padding:0 8px;")
            self._last_auto_mode = None

    def _send_command(self, text: str):
        if not self._worker.is_connected():
            QMessageBox.warning(self, "提示", "请先连接串口")
            return
        upper = text.upper()
        if upper.startswith("*SET:TIME"):
            self._pending_set = "TIME"
        elif upper.startswith("*SET:DATE"):
            self._pending_set = "DATE"
        elif upper.startswith("*SET:ALARM"):
            self._pending_set = "ALARM_OFF" if "OFF" in upper else "ALARM"
        self._worker.send(text)
        self._log_panel.add_log(MsgDir.SEND, text)

    def _speak_set_result(self):
        cp = self._control
        if self._pending_set == "TIME":
            self._announcer.speak(
                f"时间已设置为{cp.spin_hour.value()}点{cp.spin_min.value()}分{cp.spin_sec.value()}秒")
        elif self._pending_set == "DATE":
            self._announcer.speak(
                f"日期已设置为{cp.spin_year.value()}年{cp.spin_month.value()}月{cp.spin_day.value()}日")
        elif self._pending_set == "ALARM":
            self._announcer.speak(
                f"闹钟已设置为{cp.spin_ah.value()}点{cp.spin_am.value()}分{cp.spin_as.value()}秒")
        elif self._pending_set == "ALARM_OFF":
            self._announcer.speak("闹钟已关闭")
        self._pending_set = None

    # ── Display sync ──────────────────────────────────────────────

    def _on_virtual_key_pressed(self, name: str):
        """Virtual key clicked in digital twin — send *SET:KEY + local action."""
        self._send_command(f"*SET:KEY {name}")
        if name == "USER1":
            self._ntp_sync()
        elif name == "USER2":
            self._fetch_weather()

    # ── NTP time sync ────────────────────────────────────────────

    def _ntp_sync(self, quiet=False):
        """Query NTP server and send *SET:TIME / *SET:DATE to S800.

        Args:
            quiet: If True, suppress QMessageBox (for auto-sync).
        """
        if not self._worker.is_connected():
            if not quiet:
                QMessageBox.warning(self, "提示", "请先连接串口")
            return
        self._log_panel.add_log(MsgDir.SYSTEM, "NTP对时: 正在查询时间服务器...")
        try:
            now = self._query_ntp()
        except Exception as exc:
            self._log_panel.add_log(MsgDir.ERROR, f"NTP查询失败: {exc}")
            if not quiet:
                QMessageBox.warning(self, "NTP错误", f"无法获取NTP时间:\n{exc}")
                self._announcer.speak("NTP查询失败")
            return

        # Calculate NTP delta (seconds between local and NTP time)
        ntp_epoch = time.mktime(now)
        local_epoch = time.time()
        delta = abs(ntp_epoch - local_epoch)
        self._event_logger.log("SYNC", f"{delta:.3f}")

        # Send time: *SET:TIME HOUR <h> MIN <m> SEC <s>
        time_cmd = f"*SET:TIME HOUR {now.tm_hour} MIN {now.tm_min} SEC {now.tm_sec}"
        self._send_command(time_cmd)

        # Send date: *SET:DATE YEAR <y> MONTH <m> DATE <d>
        date_cmd = f"*SET:DATE YEAR {now.tm_year} MONTH {now.tm_mon} DATE {now.tm_mday}"
        self._send_command(date_cmd)

        self._log_panel.add_log(MsgDir.SYSTEM, "NTP对时完成")
        if not quiet:
            self._announcer.speak("NTP对时完成")

    # ── Weather fetch ────────────────────────────────────────────

    def _fetch_weather(self):
        """Fetch local weather and send *SET:WEA to S800."""
        if not self._worker.is_connected():
            QMessageBox.warning(self, "提示", "请先连接串口")
            return
        self._log_panel.add_log(MsgDir.SYSTEM, "天气: 正在获取...")
        try:
            temp, cond = self._query_weather()
        except Exception as exc:
            self._log_panel.add_log(MsgDir.ERROR, f"天气获取失败: {exc}")
            QMessageBox.warning(self, "天气错误", f"无法获取天气:\n{exc}")
            self._announcer.speak("天气获取失败")
            return
        temp = max(-40, min(50, temp))
        cmd = f"*SET:WEA {temp} {cond}"
        self._send_command(cmd)
        cond_cn = {"SUN": "晴", "CLD": "多云", "OVC": "阴", "RAI": "雨", "SNO": "雪", "FOG": "雾"}
        self._log_panel.add_log(MsgDir.SYSTEM, f"天气已下发: {temp}C {cond}")
        self._event_logger.log("WEATHER", f"{temp} {cond}")
        self._announcer.speak(f"天气已获取，{temp}度，{cond_cn.get(cond, cond)}")

    @staticmethod
    def _query_weather():
        """Fetch weather from wttr.in — returns (temp_c, s800_cond)."""
        import urllib.request
        import json

        url = "https://wttr.in/?format=j1"
        req = urllib.request.Request(url, headers={"User-Agent": "S800-Host"})
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read())

        cur = data["current_condition"][0]
        temp = int(cur["temp_C"])
        code = cur["weatherCode"]

        # Map wttr.in codes → S800: SUN/CLD/OVC/RAI/SNO/FOG
        cond_map = {
            "113": "SUN",  # Clear/Sunny
            "116": "CLD",  # Partly cloudy
            "119": "CLD",  # Cloudy
            "122": "OVC",  # Overcast
            "143": "FOG",  # Mist
            "176": "RAI",  # Patchy rain
            "179": "SNO",  # Patchy snow
            "182": "SNO",  # Patchy sleet
            "200": "RAI",  # Thundery outbreaks
            "227": "SNO",  # Blowing snow
            "230": "SNO",  # Blizzard
            "248": "FOG",  # Fog
            "260": "FOG",  # Freezing fog
            "263": "RAI",  # Patchy light drizzle
            "266": "RAI",  # Light drizzle
            "281": "RAI",  # Freezing drizzle
            "284": "RAI",  # Heavy freezing drizzle
            "293": "RAI",  # Patchy light rain
            "296": "RAI",  # Light rain
            "299": "RAI",  # Moderate rain at times
            "302": "RAI",  # Moderate rain
            "305": "RAI",  # Heavy rain at times
            "308": "RAI",  # Heavy rain
            "311": "RAI",  # Light freezing rain
            "314": "RAI",  # Moderate or heavy freezing rain
            "317": "RAI",  # Light sleet
            "320": "SNO",  # Moderate or heavy sleet
            "323": "SNO",  # Patchy light snow
            "326": "SNO",  # Light snow
            "329": "SNO",  # Patchy moderate snow
            "332": "SNO",  # Moderate snow
            "335": "SNO",  # Patchy heavy snow
            "338": "SNO",  # Heavy snow
            "350": "RAI",  # Ice pellets
            "353": "RAI",  # Light rain shower
            "356": "RAI",  # Moderate or heavy rain shower
            "359": "RAI",  # Torrential rain shower
            "362": "SNO",  # Light sleet showers
            "365": "SNO",  # Moderate or heavy sleet showers
            "368": "SNO",  # Light snow showers
            "371": "SNO",  # Moderate or heavy snow showers
            "374": "RAI",  # Ice pellets showers
            "377": "RAI",  # Ice pellets showers
            "386": "RAI",  # Patchy light rain with thunder
            "389": "RAI",  # Moderate or heavy rain with thunder
            "392": "RAI",  # Patchy light snow with thunder
            "395": "SNO",  # Moderate or heavy snow with thunder
        }
        cond = cond_map.get(code, "CLD")
        return temp, cond

    @staticmethod
    def _query_ntp():
        """Minimal NTP client — returns time.struct_time in UTC."""
        import socket
        import struct

        NTP_SERVER = "pool.ntp.org"
        NTP_PORT = 123
        NTP_PACKET = b"\x1b" + 47 * b"\0"
        # 1970-01-01 00:00:00 in NTP seconds = 2208988800
        NTP_DELTA = 2208988800

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(3)
        sock.sendto(NTP_PACKET, (NTP_SERVER, NTP_PORT))
        data, _ = sock.recvfrom(1024)
        sock.close()

        # NTP timestamp is seconds since 1900-01-01, big-endian 64-bit
        # The integer part starts at byte 40 (transmit timestamp)
        buf = data[40:48]
        ntp_sec = struct.unpack("!I", buf[:4])[0] - NTP_DELTA
        return time.localtime(ntp_sec)

    # ── Receive handling ───────────────────────────────────────────

    def _on_line_received(self, line: str):
        msg = parse_message(line)
        if msg.type == MsgType.RESPONSE:
            kw = msg.keyword.upper()
            # Unpack *EVT messages — two possible formats:
            #   *EVT:DISP:12345678:FF  → kw="*EVT:"  value="DISP:12345678:FF"
            #   *EVT:DISP 12345678 FF  → kw="*EVT:DISP" value="12345678 FF"
            if kw.startswith("*EVT"):
                if kw == "*EVT:":
                    # Colon format: value = "DISP:12345678:FF"
                    if ":" in msg.value:
                        sub, data = msg.value.split(":", 1)
                    else:
                        sub, data = msg.value, ""
                else:
                    # Space format: kw = "*EVT:DISP", value = "12345678 FF"
                    sub = kw[5:]   # strip "*EVT:" → "DISP"
                    data = msg.value
                self._handle_response(f"*{sub}:", data)
                direction = MsgDir.EVENT
                # *EVT serves as 1 Hz heartbeat
                self._twin.pulse_heartbeat_led()
                self._lbl_hb.setText("♥")
                self._lbl_hb.setStyleSheet(f"color:{get_theme()['lbl_hb_flash']};font-size:14px;padding:0 8px;")
                QTimer.singleShot(500, self._reset_hb_icon)
            else:
                self._handle_response(msg.keyword, msg.value)
                if kw in ("*DISP:", "*LED:", "*KEY:"):
                    direction = MsgDir.EVENT
                else:
                    direction = MsgDir.RECV
            self._log_panel.add_log(direction, line)
        elif msg.type == MsgType.ERROR:
            self._log_panel.add_log(MsgDir.ERROR, line)
        else:
            if msg.keyword == "OK" and self._pending_set:
                self._speak_set_result()
            self._log_panel.add_log(MsgDir.RECV, line)

    def _handle_response(self, keyword: str, value: str):
        kw = keyword.upper()

        if kw == "*TIME:":
            text8, _ = parse_time_response(value)
            self._last_time_text = text8
            self._twin.apply_time(text8)

        elif kw == "*DATE:":
            pass  # logged; no twin action

        elif kw == "*ALARM:":
            if not value.strip():
                # *EVT:ALARM — alarm triggered (ringing)
                self._event_logger.log("ALARM_TRIGGER", "")
            else:
                _, enabled = parse_alarm_response(value)
                self._alarm_enabled = enabled
                self._twin.set_alarm_led(enabled)
                self._event_logger.log("ALARM", "ON" if enabled else "OFF")
                self._announcer.speak("闹钟已开启" if enabled else "闹钟已关闭")
            self._update_status_labels()

        elif kw == "*DISPLAY:":
            self._display_on = "ON" in value.upper()
            self._update_status_labels()

        elif kw == "*FORMAT:":
            self._format = FormatMode.RIGHT if "RIGHT" in value.upper() else FormatMode.LEFT
            self._update_status_labels()

        elif kw == "*DISP:":
            text8, dp_mask = parse_disp(value)
            self._last_time_text = text8
            self._twin.apply_disp(text8, dp_mask)

        elif kw == "*LED:":
            mask = parse_led(value)
            self._twin.apply_led(mask)

        elif kw == "*MODE:":
            if "NIGHT" in value.upper():
                self._mode = DisplayMode.NIGHT
            else:
                self._mode = DisplayMode.DAY
            self._last_auto_mode = self._mode
            self._event_logger.log("MODE", self._mode.value)
            self._twin.apply_mode(self._mode)
            self._update_status_labels()

        elif kw == "*SET:WEA" and value.strip().upper() != "OK":
            temp, cond = parse_weather_response(value)
            cond_names = {"SUN": "晴", "CLD": "多云", "OVC": "阴",
                          "RAI": "雨", "SNO": "雪", "FOG": "雾"}
            name = cond_names.get(cond, cond)
            if cond in ("RAI", "SNO"):
                self._twin.start_wx_rain_blink()
            else:
                self._twin.stop_wx_rain_blink()

        elif kw == "*KEY:":
            name = value.upper().strip()
            self._event_logger.log("KEY", name)
            if name == "USER1":
                self._log_panel.add_log(MsgDir.SYSTEM, "*EVT:KEY USER1 → 自动触发NTP对时")
                self._ntp_sync()
            elif name == "USER2":
                self._log_panel.add_log(MsgDir.SYSTEM, "*EVT:KEY USER2")

        elif kw == "*RST":
            self._mode = DisplayMode.DAY
            self._format = FormatMode.LEFT
            self._alarm_enabled = False
            self._display_on = True
            self._last_auto_mode = None
            self._twin.stop_wx_rain_blink()
            self._twin.apply_mode(DisplayMode.DAY)
            self._twin.apply_time("        ")
            self._twin.apply_led(0xFF)
            self._update_status_labels()

        elif kw.startswith("*SET:"):
            if "MODE" in kw:
                self._mode = DisplayMode.NIGHT if self._control.btn_mn.isChecked() else DisplayMode.DAY
                self._last_auto_mode = self._mode
                self._twin.apply_mode(self._mode)
            self._update_status_labels()


    def _on_pong(self, uptime: int):
        t = get_theme()
        self._pong_count += 1
        self._twin.pulse_heartbeat_led()
        self._lbl_hb.setText("♥")
        self._lbl_hb.setStyleSheet(f"color:{t['lbl_hb_flash']};font-size:14px;padding:0 8px;")
        QTimer.singleShot(500, self._reset_hb_icon)

    def _reset_hb_icon(self):
        self._lbl_hb.setText("♡")
        self._lbl_hb.setStyleSheet(f"color:{get_theme()['lbl_dim']};font-size:14px;padding:0 8px;")

    def _on_error(self, error_msg: str):
        self._log_panel.add_log(MsgDir.ERROR, error_msg)
        if any(kw in error_msg for kw in ["无法打开", "占用", "异常", "Cannot open", "Serial", "Error"]):
            QMessageBox.critical(self, "串口错误", error_msg)
        elif "S800:" in error_msg or "S800错误" in error_msg:
            QMessageBox.warning(self, "S800 错误", error_msg)

    def _toggle_theme(self):
        t = LIGHT_THEME if get_theme()["name"] == "dark" else DARK_THEME
        set_theme(t)
        self._apply_theme()
        self._log_panel.add_log(MsgDir.SYSTEM,
                                f"主题: {'浅色' if t['name'] == 'light' else '深色'}")

    def _apply_theme(self):
        t = get_theme()
        is_dark = t["name"] == "dark"

        # Update theme toggle button
        self._btn_theme.setText("☀ 浅色模式" if is_dark else "🌙 深色模式")
        btn_fg = t["lbl_dim"]
        self._btn_theme.setStyleSheet(
            f"QPushButton{{background:transparent;color:{btn_fg};border:none;"
            f"padding:4px 12px;font-size:12px;}}"
            f"QPushButton:hover{{background:{t['btn_hover']};border-radius:3px;}}")

        # Global stylesheet
        self.setStyleSheet(f"""
            QMainWindow, QWidget {{
                background-color: {t['bg']}; color: {t['fg']};
                font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            }}
            QGroupBox {{
                border: 1px solid {t['group_border']}; border-radius: 6px;
                margin-top: 12px; padding-top: 16px;
                font-weight: bold; color: {t['group_title']};
            }}
            QGroupBox::title {{ subcontrol-origin: margin; left: 12px; padding: 0 6px; }}
            QComboBox {{
                background: {t['input_bg']}; color: {t['input_fg']};
                border: 1px solid {t['input_border']}; border-radius: 3px; padding: 3px 6px;
            }}
            QComboBox::drop-down {{ border: none; }}
            QComboBox QAbstractItemView {{
                background: {t['input_bg']}; color: {t['input_fg']};
                selection-background-color: {t['input_sel_bg']}; min-width: 140px;
            }}
            QLineEdit {{
                background: {t['input_bg']}; color: {t['input_fg']};
                border: 1px solid {t['input_border']}; border-radius: 3px; padding: 4px 8px;
            }}
            QSpinBox {{
                background: {t['input_bg']}; color: {t['input_fg']};
                border: 1px solid {t['input_border']}; border-radius: 3px; padding: 3px 6px;
            }}
            QPushButton {{
                background: {t['btn_bg']}; color: {t['btn_fg']};
                border: 1px solid {t['btn_border']}; border-radius: 3px;
                padding: 6px 14px; min-height: 28px;
            }}
            QPushButton:hover {{ background: {t['btn_hover']}; }}
            QPushButton:pressed {{ background: {t['btn_pressed']}; }}
            QRadioButton {{ color: {t['radio_fg']}; spacing: 4px; }}
            QStatusBar {{
                background: {t['statusbar_bg']};
                border-top: 1px solid {t['statusbar_border']};
            }}
            QSplitter::handle {{ background: {t['splitter']}; width: 2px; }}
        """)

        # LogPanel table
        self._log_panel.table.setStyleSheet(
            f"QTableWidget{{alternate-background-color:{t['table_alt']};"
            f"background:{t['table_bg']};color:{t['table_fg']};"
            f"gridline-color:{t['table_grid']};}}"
            f"QHeaderView::section{{background:{t['table_header_bg']};"
            f"color:{t['table_header_fg']};border:1px solid {t['table_grid']};}}")

        # LogPanel buttons
        log_btn_style = (
            f"QPushButton{{background:{t['log_btn_bg']};color:{t['log_btn_fg']};"
            f"padding:4px 10px;border-radius:3px;}}"
            f"QPushButton:hover{{background:{t['btn_hover']};}}"
            f"QPushButton:checked{{background:{t['log_btn_checked']};}}")
        self._log_panel.btn_export.setStyleSheet(log_btn_style)
        self._log_panel.btn_clear.setStyleSheet(log_btn_style)
        self._log_panel.chk_auto.setStyleSheet(log_btn_style)
        self._log_panel.update_theme(t)

        # Right-side tabs
        self._right_tabs.setStyleSheet(f"""
            QTabWidget::pane {{ border: 1px solid {t['group_border']}; background: {t['tab_pane_bg']}; }}
            QTabBar::tab {{
                background: {t['tab_bg']}; color: {t['tab_fg']};
                padding: 6px 16px; border: 1px solid {t['group_border']};
                border-bottom: none; border-top-left-radius: 4px;
                border-top-right-radius: 4px; margin-right: 2px;
            }}
            QTabBar::tab:selected {{
                background: {t['tab_selected_bg']}; color: {t['tab_selected_fg']};
                border-bottom: 2px solid {t['tab_selected_border']};
            }}
            QTabBar::tab:hover:!selected {{
                background: {t['tab_hover_bg']}; color: {t['tab_hover_fg']};
            }}
        """)

        # Digital twin
        self._twin._seg_frame.setStyleSheet(
            f"QFrame{{background:{t['seg_frame_bg']};"
            f"border:1px solid {t['seg_frame_border']};border-radius:4px;}}")
        for lbl in self._twin._led_labels:
            lbl.setStyleSheet(f"color:{t['led_label_fg']};font-size:9px;")
        for btn in self._twin._key_buttons:
            btn.setStyleSheet(
                f"QPushButton{{background:{t['key_btn_bg']};color:{t['key_btn_fg']};"
                f"border:1px solid {t['key_btn_border']};border-radius:4px;font-weight:bold;}}"
                f"QPushButton:hover{{background:{t['key_btn_hover']};}}"
                f"QPushButton:pressed{{background:{t['key_btn_pressed']};}}")

        # ControlPanel special buttons
        cp = self._control
        cp.btn_connect.setStyleSheet(
            f"QPushButton{{background:{t['connect_bg']};color:{t['connect_fg']};"
            f"font-weight:bold;border-radius:3px;padding:6px 16px;}}"
            f"QPushButton:hover{{background:{t['connect_hover']};}}")
        cp.btn_disconnect.setStyleSheet(
            f"QPushButton{{background:{t['disconnect_bg']};color:{t['disconnect_fg']};"
            f"font-weight:bold;border-radius:3px;padding:6px 16px;}}"
            f"QPushButton:hover{{background:{t['disconnect_hover']};}}")
        cp.btn_ntp.setStyleSheet(
            f"QPushButton{{background:{t['ntp_btn_bg']};color:{t['ntp_btn_fg']};"
            f"font-weight:bold;border-radius:3px;padding:6px 14px;}}"
            f"QPushButton:hover{{background:{t['ntp_btn_hover']};}}")
        cp.btn_weather.setStyleSheet(
            f"QPushButton{{background:{t['weather_btn_bg']};color:{t['weather_btn_fg']};"
            f"font-weight:bold;border-radius:3px;padding:6px 14px;}}"
            f"QPushButton:hover{{background:{t['weather_btn_hover']};}}")
        cp.btn_rst.setStyleSheet(
            f"QPushButton{{background:{t['rst_btn_bg']};color:{t['rst_btn_fg']};font-weight:bold;}}")
        cp.lbl_sunrise.setStyleSheet(f"color:{t['lbl_dim']};")
        cp.lbl_sunset.setStyleSheet(f"color:{t['lbl_dim']};")

        # Dashboard
        self._dashboard.set_theme(t)

        # Refresh status labels and twin display
        self._update_status_labels()
        if self._last_time_text:
            self._twin.apply_time(self._last_time_text)

    def _update_status_labels(self):
        t = get_theme()
        self._lbl_format.setText(f"FORMAT:{self._format.value}")
        self._lbl_format.setStyleSheet(f"color:{t['lbl_mid']};padding:0 8px;")
        c = t["lbl_mode_night"] if self._mode == DisplayMode.NIGHT else t["lbl_mid"]
        self._lbl_mode.setText(f"MODE:{self._mode.value}")
        self._lbl_mode.setStyleSheet(f"color:{c};padding:0 8px;")
        self._lbl_alarm.setText(f"ALARM:{'ON' if self._alarm_enabled else 'OFF'}")
        c2 = t["lbl_alarm_on"] if self._alarm_enabled else t["lbl_mid"]
        self._lbl_alarm.setStyleSheet(f"color:{c2};padding:0 8px;")
        self._lbl_disp.setText(f"DISP:{'ON' if self._display_on else 'OFF'}")
        self._lbl_disp.setStyleSheet(f"color:{t['lbl_mid']};padding:0 8px;")

    def _check_auto_mode(self):
        """Periodic check: calculate sunrise/sunset and switch mode if needed."""
        now = datetime.now()

        # Always calculate and display sunrise/sunset
        try:
            s = sun(DEFAULT_LOCATION.observer, date=now.date(),
                    tzinfo="Asia/Shanghai")
        except (ValueError, Exception):
            return

        self._sunrise_time = s["sunrise"]
        self._sunset_time = s["sunset"]

        self._control.lbl_sunrise.setText(
            f"日出: {self._sunrise_time.strftime('%H:%M')}")
        self._control.lbl_sunset.setText(
            f"日落: {self._sunset_time.strftime('%H:%M')}")

        # Auto mode switching requires connection and checkbox
        if not self._worker.is_connected():
            return
        if not self._control.chk_auto_mode.isChecked():
            return

        sunrise_t = self._sunrise_time.replace(tzinfo=None)
        sunset_t = self._sunset_time.replace(tzinfo=None)

        if sunrise_t <= now < sunset_t:
            desired = DisplayMode.DAY
        else:
            desired = DisplayMode.NIGHT

        if desired == self._last_auto_mode:
            return

        self._last_auto_mode = desired
        self._event_logger.log("MODE", desired.value)
        self._send_command(f"*SET:MODE {desired.value}")

        if desired == DisplayMode.DAY:
            self._control.btn_md.setChecked(True)
        else:
            self._control.btn_mn.setChecked(True)

        self._mode = desired
        self._twin.apply_mode(desired)
        self._update_status_labels()
        self._log_panel.add_log(
            MsgDir.SYSTEM,
            f"自动切换: {desired.value} "
            f"(日出 {self._sunrise_time.strftime('%H:%M')} "
            f"日落 {self._sunset_time.strftime('%H:%M')})")
        mode_cn = "日间模式" if desired == DisplayMode.DAY else "夜间模式"
        self._announcer.speak(f"已切换到{mode_cn}")

    def _on_auto_mode_toggled(self, state):
        if state == Qt.Checked:
            self._last_auto_mode = None
            self._log_panel.add_log(MsgDir.SYSTEM, "自动日夜切换: 已启用")
        else:
            self._log_panel.add_log(MsgDir.SYSTEM, "自动日夜切换: 已禁用")

    def _on_voice_toggled(self, state):
        self._announcer.set_enabled(state == Qt.Checked)
        if state == Qt.Checked:
            self._announcer.speak("语音播报已开启")
        self._log_panel.add_log(MsgDir.SYSTEM,
                                "语音播报: 已启用" if state == Qt.Checked else "语音播报: 已禁用")

    def _on_voice_changed(self, index):
        self._announcer.set_voice(index)
        name = VoiceAnnouncer.VOICES[index][0] if 0 <= index < len(VoiceAnnouncer.VOICES) else "?"
        self._log_panel.add_log(MsgDir.SYSTEM, f"语音音色: {name}")

    def _on_right_tab_clicked(self, index):
        self._dashboard_visible = (index == 1)
        if self._dashboard_visible:
            self._dashboard.refresh()

    def _auto_refresh_dashboard(self):
        if self._dashboard_visible:
            self._dashboard.refresh()

    def _auto_ntp_sync(self):
        """Periodically check NTP delta in background thread (no device commands)."""
        import threading
        def _query():
            try:
                now = self._query_ntp()
                ntp_epoch = time.mktime(now)
                delta = abs(ntp_epoch - time.time())
                self._event_logger.log("SYNC", f"{delta:.3f}")
            except Exception:
                pass
        threading.Thread(target=_query, daemon=True).start()

    def _refresh_ports_auto(self):
        if not self._worker.is_connected():
            self._control._refresh_ports()

    def closeEvent(self, event):
        self._scan_timer.stop()
        self._auto_mode_timer.stop()
        self._dashboard_timer.stop()
        self._ntp_timer.stop()
        self._worker.stop()
        self._thread.quit()
        self._thread.wait(2000)
        super().closeEvent(event)
