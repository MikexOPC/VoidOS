"""Unit tests for scripts/lib/serial.py (G2 Q3)."""
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("serial_log", ROOT / "scripts/lib/serial.py")
serial = importlib.util.module_from_spec(spec)
spec.loader.exec_module(serial)

SAMPLE = (
    "\x1b[33m[ WARN ]\x1b[0m KERNEL_VGPU_PRESENT_TIMEOUT\n"
    "KERNEL_VGPU_PRESENT_TIMEOUTS: 0x0000000000000001\n"
    "\x1b[1m\x1b[32m\x1b[40m[  OK  ] \x1b[0m\x1b[37m\x1b[40mKERNEL_READY\r\n"
    "[ INFO ] KERNEL_VGPU_PRESENT_RECOVERED\n"
    "KERNEL_VGPU_PRESENT_GEOMETRY: 0x0000000000000500x0x0000000000000320\n"
    "KERNEL_CONSOLE_FLUSHES: 0x00000000000004c0\n"
    "KERNEL_CONSOLE_FLUSHES: 0x00000000000004c1\n"
    "prefix KERNEL_READY inside a sentence\n"
    "[ FAIL ] PS2_INPUT_FAILED_WORKER\n"
)


class SerialLogTest(unittest.TestCase):

    def setUp(self):
        self.log = serial.SerialLog(SAMPLE)

    def test_markers_are_whole_lines(self):
        self.assertTrue(self.log.has("KERNEL_VGPU_PRESENT_TIMEOUT", "WARN"))
        self.assertFalse(self.log.has("KERNEL_VGPU_PRESENT_TIMEOUTS"))
        self.assertEqual(self.log.count("KERNEL_READY"), 1)
        self.assertTrue(self.log.has("KERNEL_READY", "OK"))
        self.assertFalse(self.log.has("KERNEL_READY", "WARN"))
        self.assertFalse(self.log.has("KERNEL_VGPU_PRESENT"))

    def test_order_and_failures(self):
        self.assertTrue(self.log.before("KERNEL_VGPU_PRESENT_TIMEOUT", "KERNEL_VGPU_PRESENT_RECOVERED"))
        self.assertFalse(self.log.before("KERNEL_VGPU_PRESENT_RECOVERED", "KERNEL_VGPU_PRESENT_TIMEOUT"))
        self.assertFalse(self.log.before("MISSING", "KERNEL_READY"))
        self.assertEqual(self.log.failures(), ["PS2_INPUT_FAILED_WORKER"])
        self.assertTrue(self.log.terminal())
        self.assertFalse(serial.SerialLog("[ OK ] KERNEL_READY\n").terminal())

    def test_numbers(self):
        self.assertEqual(self.log.number("KERNEL_VGPU_PRESENT_TIMEOUTS"), 1)
        self.assertEqual(self.log.numbers("KERNEL_CONSOLE_FLUSHES"), [0x4c0, 0x4c1])
        self.assertEqual(self.log.number("KERNEL_CONSOLE_FLUSHES", -1), 0x4c1)
        # Two values on one line are not a single number field.
        self.assertFalse(self.log.has_number("KERNEL_VGPU_PRESENT_GEOMETRY"))
        with self.assertRaises(RuntimeError):
            self.log.number("KERNEL_VGPU_PRESENT_TIMEOUT")
        with self.assertRaises(RuntimeError):
            self.log.number("KERNEL_CONSOLE_FLUSHES", 2)
        with self.assertRaises(RuntimeError):
            serial.SerialLog("").number("X", -1)

    def test_file_and_missing_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "serial.log"
            self.assertEqual(serial.SerialLog.from_file(path).events, [])
            path.write_bytes(SAMPLE.encode("utf-8") + b"\xff\n")
            self.assertTrue(serial.SerialLog.from_file(path).has("KERNEL_READY"))


if __name__ == "__main__":
    sys.exit(unittest.main(verbosity=1, exit=False).result.wasSuccessful() is False)
