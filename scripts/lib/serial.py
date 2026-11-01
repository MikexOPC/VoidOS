"""G2 Q3: kernel serial log read as events instead of substrings.

Lines are matched whole after the ANSI colour codes are removed:
  [ OK ] NAME / [ INFO ] NAME / [ WARN ] NAME / [ FAIL ] NAME   -> markers
  FIELD: 0x0123456789abcdef                                     -> numbers
so KERNEL_VGPU_PRESENT_TIMEOUT never matches KERNEL_VGPU_PRESENT_TIMEOUTS.
"""
import re
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
MARKER = re.compile(r"^\[\s*(OK|INFO|WARN|FAIL)\s*\]\s+([A-Z0-9_]+)\s*$")
FIELD = re.compile(r"^([A-Z0-9_]+): 0x([0-9a-fA-F]{16})(?![0-9a-fA-Fx])")
# Terminal kernel states: a runner waiting for a marker stops at the first of these.
FAILURE = re.compile(r"KERNEL_EXCEPTION_HALTED|\bKERNEL_SMP_PANIC(?:_HALTED)?\b|\bPANIC\b|DISPATCH_FAILED|PS2_INPUT_FAILED|KERNEL_CONSOLE_DRAW_REJECTED_PM")


# ====== strip ======

def strip(text):
    return ANSI.sub("", text)


# ====== SerialLog ======

class SerialLog:
    """One snapshot of the serial output; build a new one to see later lines."""

    def __init__(self, text):
        self.text = strip(text)
        self.lines = self.text.splitlines()
        self.events = []
        self.fields = {}
        for index, line in enumerate(self.lines):
            line = line.strip()
            marker = MARKER.match(line)
            if marker:
                self.events.append((index, marker[1], marker[2]))
                continue
            field = FIELD.match(line)
            if field:
                self.fields.setdefault(field[1], []).append(int(field[2], 16))

    # ====== from_file ======

    @classmethod
    def from_file(cls, path):
        path = Path(path)
        return cls(path.read_text(encoding="utf-8", errors="replace") if path.exists() else "")

    # ====== markers ======

    def count(self, name, level=None):
        return sum(1 for _, kind, marker in self.events if marker == name and (level is None or kind == level))

    def has(self, name, level=None):
        return self.count(name, level) > 0

    def line(self, name):
        """Line index of the first occurrence of the marker, or None."""
        return next((index for index, _, marker in self.events if marker == name), None)

    def before(self, first, second):
        a, b = self.line(first), self.line(second)
        return a is not None and b is not None and a < b

    def failures(self):
        return [marker for _, kind, marker in self.events if kind == "FAIL"]

    def terminal(self):
        return bool(FAILURE.search(self.text))

    # ====== numbers ======

    def number(self, field, index=0):
        values = self.fields.get(field, [])
        if not -len(values) <= index < len(values):
            raise RuntimeError("Missing numeric evidence: " + field)
        return values[index]

    def numbers(self, field):
        return list(self.fields.get(field, []))

    def has_number(self, field):
        return field in self.fields
