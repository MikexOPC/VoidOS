import importlib.util
from pathlib import Path
import sys
import unittest

spec = importlib.util.spec_from_file_location("service_firmware", Path(__file__).with_name("firmware.py"))
firmware = importlib.util.module_from_spec(spec)
spec.loader.exec_module(firmware)


# ====== RegisterParsing ======

class RegisterParsing(unittest.TestCase):
    def test_hmp_padding(self):
        text = "R8 =0000000012345678 R9 =0000000012345678 R10=0000000012345678"
        for name in ("R8", "R9", "R10"):
            self.assertEqual(firmware.register_value(text, name), 0x12345678)

    def test_exact_name(self):
        self.assertEqual(firmware.register_value("CR8=ff R8 =aa", "R8"), 0xaa)
        with self.assertRaises(RuntimeError):
            firmware.register_value("R10=ff", "R1")

    def test_missing_or_malformed(self):
        for text in ("", "R8=oops", "R8 ="):
            with self.assertRaises(RuntimeError):
                firmware.register_value(text, "R8")

    def test_partial_panic_marker(self):
        line = "[ FAIL ] KERNEL_SMP_PANIC"
        for length in range(len(line) + 1):
            log = firmware.qemu.serial.SerialLog("[ OK ] PREVIOUS\n" + line[:length])
            self.assertEqual(firmware.complete_snapshot(log).failures(), [])
        log = firmware.qemu.serial.SerialLog(line + "\n")
        self.assertEqual(firmware.complete_snapshot(log).failures(), ["KERNEL_SMP_PANIC"])

    def test_completed_failure_remains_failure(self):
        log = firmware.qemu.serial.SerialLog("[ FAIL ] UNEXPECTED_FAULT\nPARTIAL")
        self.assertEqual(firmware.complete_snapshot(log).failures(), ["UNEXPECTED_FAULT"])


if __name__ == "__main__":
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(RegisterParsing))
    raise SystemExit(not result.wasSuccessful())
