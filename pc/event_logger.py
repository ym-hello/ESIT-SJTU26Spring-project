# -*- coding: utf-8 -*-
"""Structured event persistence to CSV for S800 PC Host."""

import csv
import os
import threading
from datetime import datetime


class EventLogger:
    """Append-only CSV event logger with thread-safe read/write."""

    COLUMNS = ["timestamp", "event_type", "value"]

    def __init__(self, path: str = "events.csv"):
        self._path = os.path.abspath(path)
        self._lock = threading.Lock()
        self._inited = False

    @property
    def path(self) -> str:
        return self._path

    def _ensure_header(self):
        """Write CSV header if missing."""
        if os.path.exists(self._path):
            try:
                with open(self._path, "r", encoding="utf-8-sig") as f:
                    first = f.readline().strip()
                if not first.startswith("timestamp"):
                    with open(self._path, "r", encoding="utf-8-sig") as f:
                        old = f.read()
                    with open(self._path, "w", newline="", encoding="utf-8-sig") as f:
                        w = csv.writer(f)
                        w.writerow(self.COLUMNS)
                        f.write(old)
            except OSError:
                pass
        else:
            with open(self._path, "w", newline="", encoding="utf-8-sig") as f:
                csv.writer(f).writerow(self.COLUMNS)
        self._inited = True

    def log(self, event_type: str, value: str = ""):
        """Append one event row to the CSV file."""
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        with self._lock:
            try:
                if not self._inited:
                    self._ensure_header()
                with open(self._path, "a", newline="", encoding="utf-8-sig") as f:
                    csv.writer(f).writerow([ts, event_type, value])
            except OSError:
                pass

    def load(self) -> list:
        """Read all rows from the CSV. Returns list of dicts.

        Returns empty list on any error (missing file, malformed data, etc.).
        """
        if not os.path.exists(self._path):
            return []
        rows = []
        with self._lock:
            try:
                with open(self._path, "r", newline="", encoding="utf-8-sig") as f:
                    reader = csv.DictReader(f)
                    for row in reader:
                        if all(c in row for c in self.COLUMNS):
                            rows.append(row)
            except (OSError, csv.Error):
                return []
        return rows
