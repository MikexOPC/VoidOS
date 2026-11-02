import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("service_firmware", Path(__file__).with_name("firmware.py"))
firmware = importlib.util.module_from_spec(spec)
spec.loader.exec_module(firmware)

group_spec = importlib.util.spec_from_file_location("service_group_tests", Path(__file__).parents[2] / "panic/integration/group.py")
group = importlib.util.module_from_spec(group_spec)
group_spec.loader.exec_module(group)


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

    def test_failed_boot_captures_requested_cpus(self):
        for count in (2, 8):
            with self.subTest(count=count), tempfile.TemporaryDirectory(dir=firmware.transition.ROOT / "kernel/build") as directory:
                root = Path(directory)
                (root / "kernel/build").mkdir(parents=True)
                original = RuntimeError("original boot failure")
                class Vm:
                    def __init__(self, path, image, video, smp):
                        self.directory = path
                        path.mkdir()
                        self.count = smp
                    def __enter__(self): return self
                    def __exit__(self, *error): return False
                    def command(self, command): self.stopped = command == "stop"
                with patch.object(firmware.transition, "ROOT", root), patch.object(firmware.qemu, "Vm", Vm), \
                    patch.object(firmware.qemu, "finish") as finish, \
                    patch.object(firmware, "await_terminal", side_effect=original), \
                    patch.object(firmware.launch, "registers", side_effect=lambda vm, i: f"CPU{i}") as registers, \
                    patch.object(sys, "argv", ["firmware.py", "--image", "unused.elf", "--smp", str(count), "--boots", "1"]):
                    with self.assertRaises(RuntimeError) as caught:
                        firmware.main()
                    self.assertIs(caught.exception, original)
                    self.assertEqual([call.args[1] for call in registers.call_args_list], list(range(count)))
                    self.assertEqual(len(list(root.rglob("failure-cpu-*.txt"))), count)
                    self.assertEqual(finish.call_args.args[3], [{"Failure": str(original)}])

    def test_capture_error_preserves_original_failure(self):
        with tempfile.TemporaryDirectory(dir=firmware.transition.ROOT / "kernel/build") as directory:
            root = Path(directory)
            (root / "kernel/build").mkdir(parents=True)
            original = RuntimeError("boot failed")
            class Vm:
                def __init__(self, *args, **kwargs): pass
                def __enter__(self): return self
                def __exit__(self, *error): return False
            with patch.object(firmware.transition, "ROOT", root), patch.object(firmware.qemu, "Vm", Vm), \
                patch.object(firmware.qemu, "finish"), patch.object(firmware, "await_terminal", side_effect=original), \
                patch.object(firmware, "capture_failure", side_effect=OSError("monitor lost")), \
                patch.object(sys, "argv", ["firmware.py", "--image", "unused.elf", "--boots", "1"]):
                with self.assertRaises(RuntimeError) as caught:
                    firmware.main()
                self.assertIs(caught.exception, original)
                self.assertIn("monitor lost", original.__notes__[0])

    def test_settle_visits_the_whole_group(self):
        for count in (2, 4, 8):
            with self.subTest(count=count):
                def registers(vm, index):
                    self.assertLess(index, count)
                    return "HLT=1 RAX=" + format(group.DONE if index == 0 else group.STOP, "016x")
                with patch.object(group.launch, "registers", side_effect=registers) as read:
                    result = group.settle(object(), 0, count)
                    self.assertEqual(len(result), count)
                    self.assertEqual([call.args[1] for call in read.call_args_list], list(range(count)))
                    if count == 4:
                        self.assertEqual(len(group.settle(object(), 0)), 4)

    def test_settle_waits_for_the_last_cpu(self):
        class Vm:
            ready = False
            def command(self, command):
                if command == "cont": self.ready = True
        vm = Vm()
        def registers(vm, index):
            halt = int(index != 7 or vm.ready)
            return f"HLT={halt} RAX=" + format(group.DONE if index == 0 else group.STOP, "016x")
        with patch.object(group.launch, "registers", side_effect=registers) as read, patch.object(group.time, "sleep"):
            self.assertEqual(len(group.settle(vm, 0, 8)), 8)
            self.assertTrue(vm.ready)
            self.assertEqual(read.call_count, 16)

    def test_settle_rejects_invalid_groups(self):
        with patch.object(group.launch, "registers") as read:
            for origin, count in ((0, 0), (0, 65), (-1, 4), (4, 4)):
                with self.assertRaises(ValueError):
                    group.settle(object(), origin, count)
            read.assert_not_called()


if __name__ == "__main__":
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(RegisterParsing))
    raise SystemExit(not result.wasSuccessful())
