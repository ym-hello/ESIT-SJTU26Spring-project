# -*- coding: utf-8 -*-
"""S800 clock serial protocol — all commands start with '*'."""

from enum import Enum
from dataclasses import dataclass
from typing import Tuple

# ── Message type ──────────────────────────────────────────────────

class MsgType(Enum):
    COMMAND = "COMMAND"   # sent by PC
    RESPONSE = "RESPONSE"  # received from S800 (*CMD OK, *CMD: value)
    ERROR = "ERROR"        # ERROR ...
    UNKNOWN = "UNKNOWN"

class MsgDir(Enum):
    SEND = "SEND"
    RECV = "RECV"
    EVENT = "EVENT"
    ERROR = "ERROR"
    SYSTEM = "SYSTEM"

# ── Enums matching actual S800 vocabulary ─────────────────────────

class DisplayMode(Enum):
    DAY = "DAY"
    NIGHT = "NIGHT"

class FormatMode(Enum):
    LEFT = "LEFT"
    RIGHT = "RIGHT"

class KeyName(Enum):
    FUNC = "FUNC"
    SHIFT = "SHIFT"
    ADD = "ADD"
    SAVE = "SAVE"
    DISP = "DISP"
    SPEED = "SPEED"
    FORMAT = "FORMAT"
    EXT = "EXT"
    USER1 = "USER1"
    USER2 = "USER2"

# ── Constants ─────────────────────────────────────────────────────
BAUD_RATES = [9600, 14400, 19200, 38400, 57600, 115200]
DEFAULT_BAUD = 115200

# ── 7‑segment patterns (same as before) ───────────────────────────
SEG7_PATTERNS: dict[str, int] = {
    '0': 0x3F, '1': 0x06, '2': 0x5B, '3': 0x4F,
    '4': 0x66, '5': 0x6D, '6': 0x7D, '7': 0x07,
    '8': 0x7F, '9': 0x6F,
    'A': 0x77, 'a': 0x77, 'B': 0x7C, 'b': 0x7C,
    'C': 0x39, 'c': 0x39, 'D': 0x5E, 'd': 0x5E,
    'E': 0x79, 'e': 0x79, 'F': 0x71, 'f': 0x71,
    'G': 0x3D, 'g': 0x3D, 'H': 0x76, 'h': 0x76,
    'I': 0x30, 'i': 0x30, 'J': 0x1E, 'j': 0x1E,
    'K': 0x7A, 'k': 0x7A, 'L': 0x3C, 'l': 0x3C,
    'M': 0x55, 'm': 0x55, 'N': 0x37, 'n': 0x37,
    'O': 0x3F, 'o': 0x3F, 'P': 0x73, 'p': 0x73,
    'Q': 0x67, 'q': 0x67, 'R': 0x70, 'r': 0x70,
    'S': 0x6D, 's': 0x6D, 'T': 0x78, 't': 0x78,
    'U': 0x3E, 'u': 0x3E, 'V': 0x7E, 'v': 0x7E,
    'W': 0x6A, 'w': 0x6A, 'X': 0x36, 'x': 0x36,
    'Y': 0x6E, 'y': 0x6E, 'Z': 0x49, 'z': 0x49,
    '-': 0x40, '_': 0x00, '=': 0x48,
    ' ': 0x00, '.': 0x00, ':': 0x00, '°': 0x63,
}

def seg7_pattern(ch: str) -> int:
    return SEG7_PATTERNS.get(ch, SEG7_PATTERNS.get(ch.upper(),
                              SEG7_PATTERNS.get(ch.lower(), 0)))

# ── Data class ────────────────────────────────────────────────────

@dataclass
class ParsedMessage:
    raw: str
    type: MsgType
    keyword: str   # e.g. "PONG", "TIME", "SET:TIME", "SET:FORMAT"
    value: str     # everything after the keyword


# ── Message builder ───────────────────────────────────────────────

def build_cmd(base: str, *args: str) -> str:
    """Build a command: build_cmd('*SET:TIME','HOUR','8') → '*SET:TIME HOUR 8'"""
    if args:
        return f"{base} {' '.join(args)}"
    return base

def build_colon_cmd(cmd: str, *args: str) -> str:
    """Build colon‑style: build_colon_cmd('*SET:KEY','FUNC') → '*SET:KEY FUNC'"""
    if args:
        return f"{cmd} {' '.join(args)}"
    return cmd

# ── Message parser ────────────────────────────────────────────────

def parse_message(line: str) -> ParsedMessage:
    """Parse one line received from S800.

    Recognised patterns:
      *PONG <seconds>
      *TIME: <HH:MM:SS>
      *DATE: <YYYY-MM-DD>
      *ALARM: <HH:MM:SS> ON/OFF
      *DISPLAY: ON/OFF
      *FORMAT: LEFT/RIGHT
      *SET:TIME OK
      *SET:DATE OK
      *SET:ALARM OK
      *SET:DISPLAY OK
      *SET:FORMAT OK
      *SET:MSG OK
      *SET:BEEP OK
      *SET:LED OK
      *SET:KEY OK
      *SET:MODE OK
      *RST OK
      ERROR <detail>
    """
    line = line.strip()
    if not line:
        return ParsedMessage(line, MsgType.UNKNOWN, "", "")

    upper = line.upper()

    # ERROR
    if upper.startswith("ERROR"):
        return ParsedMessage(line, MsgType.ERROR, "ERROR", line[5:].strip())

    # Must start with *
    if not line.startswith("*"):
        return ParsedMessage(line, MsgType.UNKNOWN, line, "")

    # Split header from value.  Space‑separated (*PONG 1234, *TIME: 12:34:56)
    # or colon‑separated (*DISP:12345678:FF, *LED:FF).
    if " " in line:
        header, rest = line.split(" ", 1)
    elif ":" in line and line[0] == "*":
        idx = line.index(":")
        header = line[:idx + 1]   # include the colon: "*DISP:"
        rest = line[idx + 1:]     # everything after: "12345678:FF"
    else:
        header, rest = line, ""

    # All recognised responses use the original header as keyword
    # so the handler can match on *PONG, *TIME:, *DATE:, *SET:TIME, etc.
    header_upper = header.upper()

    if header_upper in ("*PONG", "*TIME:", "*DATE:", "*ALARM:",
                         "*DISPLAY:", "*FORMAT:", "*DISP:", "*LED:",
                         "*SET:WEA"):
        return ParsedMessage(line, MsgType.RESPONSE, header, rest)

    # OK responses: *SET:TIME OK, *SET:DATE OK, *RST OK, etc.
    if rest.upper() == "OK" or rest == "OK":
        return ParsedMessage(line, MsgType.RESPONSE, header, rest)

    # Unknown but well-formed * response
    return ParsedMessage(line, MsgType.RESPONSE, header, rest)


# ── Response parsers ──────────────────────────────────────────────

def parse_time_response(value: str) -> Tuple[str, int]:
    """'12:34:56' → ('123456  ', dp_mask for colon positions)."""
    raw = value.strip()
    dp_mask = 0
    # Colons in raw string → dp on the digit that precedes them
    digit_idx = 0
    for ch in raw:
        if ch == ":":
            if digit_idx > 0:
                dp_mask |= 1 << (digit_idx - 1)
        else:
            digit_idx += 1
    digits = raw.replace(":", "")
    return digits[:6].ljust(8), dp_mask

def parse_date_response(value: str) -> str:
    """'2026-06-04' → '0604    ' (MMDD, left‑aligned)"""
    parts = value.strip().split("-")
    if len(parts) == 3:
        return (parts[1] + parts[2])[:8].ljust(8)
    return value.strip()[:8].ljust(8)

def parse_alarm_response(value: str) -> Tuple[str, bool]:
    """'12:34:56 ON' → ('123456  ', True)"""
    v = value.strip()
    enabled = v.upper().endswith(" ON")
    time_part = v.rsplit(" ", 1)[0].replace(":", "")
    return time_part[:6].ljust(8), enabled


def parse_disp(value: str) -> Tuple[str, int]:
    """Parse *DISP value → (text8, dp_bitmask).

    Accepts both formats:
      "12345678:FF"   (colon separator)
      "12345678 FF"   (space separator)
    """
    v = value.strip()
    # Try colon first, then space
    if ":" in v:
        parts = v.split(":")
    else:
        parts = v.split()
    text = parts[0][:8].ljust(8) if parts else " " * 8
    dp = 0
    if len(parts) >= 2:
        try:
            dp = int(parts[1], 16) & 0xFF
        except ValueError:
            dp = 0
    return text, dp


def parse_led(value: str) -> int:
    """Parse *LED value → bitmask 0-255."""
    try:
        return int(value.strip(), 16) & 0xFF
    except ValueError:
        return 0


def parse_weather_response(value: str) -> Tuple[int, str]:
    """Parse *SET:WEA value → (temp_c, cond_code).

    Accepts: "-5 SUN", "25 CLD", "0 OVC"
    Returns: (temperature, condition_code)
    """
    parts = value.strip().split()
    if len(parts) < 2:
        return 0, "SUN"
    try:
        temp = int(parts[0])
    except ValueError:
        temp = 0
    cond = parts[1].upper()
    if cond not in ("SUN", "CLD", "OVC", "RAI", "SNO", "FOG"):
        cond = "SUN"
    return temp, cond

