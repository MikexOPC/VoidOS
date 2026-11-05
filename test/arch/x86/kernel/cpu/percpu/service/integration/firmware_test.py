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

    # ====== concurrent_fixture ======

    @staticmethod
    def concurrent_fixture():
        root, pool, free, first = 0x1000, 0x2000, 1000, 0xffffffff80105000
        names = dict(KERNEL_AP_BATCH_RECORD=0xffffffff80110000, KERNEL_AP_SECOND_RECORD=0xffffffff80110400,
                     KERNEL_AP_PREEMPT_RECORD=0xffffffff80110800, KERNEL_AP_SECOND_PREEMPT=0xffffffff80110c00,
                     KERNEL_AP_BATCH_PTE=0xffffffff80111000, KERNEL_AP_LOT_RECORD=0xffffffff80111400,
                     KERNEL_AP_LOT_ARRIVED=0xffffffff80111800)
        bases = [0xffff810000000000 + i * 18 * 4096 for i in range(4)]
        words = {names["KERNEL_AP_BATCH_PTE"]: [0] * 72,
                 names["KERNEL_AP_LOT_RECORD"]: [0, root, 1, 6, 6, 6, 6],
                 names["KERNEL_AP_LOT_ARRIVED"]: [1, 1]}
        for slot in range(2):
            lower = slot * 2
            address = names["KERNEL_AP_BATCH_RECORD" if slot == 0 else "KERNEL_AP_SECOND_RECORD"]
            record = [0] * 48
            record[18:23] = [2, 1 << lower, bases[lower] + 8192, lower, 1]
            local = 0xffffffff80112000 + slot * 1024
            record[27:29] = [8, 2]
            record[33] = local
            record[40:44] = [0x202, 0xe1, 1, 0]
            if slot == 0:
                record[:6] = [address, root, pool, free, free - 64, free]
                record[6:18] = [16] * 4 + [136, 272, 408, 544, 17, 34, 51, 68]
                record[23:27] = [1] * 4
                record[29:33] = bases
                record[34:38] = [0xffffffff80113000 + i * 1024 for i in range(4)]
                record[38:40] = [4, 4]
            words[address] = record
            words[local] = [0] * 88
            words[local][74] = 8 | 2 << 32
            words[local][80] = 0xffffffff00000000
            words[local][82:84] = [local, first + slot * 448]
            address = names["KERNEL_AP_PREEMPT_RECORD" if slot == 0 else "KERNEL_AP_SECOND_PREEMPT"]
            words[address] = [address, 3, 3, 3, 3, 6, 1, 0] + [bases[lower + i] + 8192 for i in range(2)] * 2 + [0xffffffff80001000] * 2 + [1000, 0]
        return names, words, (root, pool, free, first)

    def test_concurrent_evidence(self):
        from types import SimpleNamespace
        names, words, values = self.concurrent_fixture()
        log = SimpleNamespace(number=lambda name: names[name])
        firmware.concurrent_batch.verify(None, log, 4, *values, lambda vm, address, count: words[address][:count])

    def test_concurrent_evidence_rejects_corruption(self):
        from types import SimpleNamespace
        mutations = [("KERNEL_AP_LOT_ARRIVED", 1, 0), ("KERNEL_AP_LOT_RECORD", 6, 2),
                     ("KERNEL_AP_BATCH_RECORD", 4, 1000), ("KERNEL_AP_BATCH_RECORD", 38, 2),
                     ("KERNEL_AP_SECOND_RECORD", 21, 0), ("KERNEL_AP_SECOND_RECORD", 42, 0),
                     ("KERNEL_AP_SECOND_PREEMPT", 5, 5), ("KERNEL_AP_SECOND_PREEMPT", 6, 0),
                     ("KERNEL_AP_BATCH_PTE", 71, 1), ("KERNEL_AP_BATCH_RECORD", 44, 1)]
        for name, index, value in mutations:
            with self.subTest(name=name, index=index):
                names, words, values = self.concurrent_fixture()
                words[names[name]][index] = value
                log = SimpleNamespace(number=lambda name: names[name])
                with self.assertRaises(RuntimeError):
                    firmware.concurrent_batch.verify(None, log, 4, *values, lambda vm, address, count: words[address][:count])

    # ====== incomplete_fixture ======

    @staticmethod
    def incomplete_fixture():
        from types import SimpleNamespace
        names, words, values = RegisterParsing.concurrent_fixture()
        record = words[names["KERNEL_AP_BATCH_RECORD"]]
        record[5] = 0
        record[38:40] = [0, 0]
        words[names["KERNEL_AP_LOT_RECORD"]][6] = 2
        live = []
        for task in range(4):
            frame = 0x400000 + task * 65536
            live += [frame, 16, task + 1, 0]
            words[names["KERNEL_AP_BATCH_PTE"]][task * 18 + 1:task * 18 + 17] = [
                (frame + page * 4096) | 3 | (1 << 63) for page in range(16)]
        log = SimpleNamespace(number=lambda name: names[name], before=lambda first, second: True)
        return names, words, log, (*values[:2], values[2] - 64), live, [0x100000001] * 2

    def test_incomplete_quiescence_keeps_live_leases(self):
        names, words, log, values, live, pins = self.incomplete_fixture()
        firmware.concurrent_batch.verify_incomplete(None, log, *values, live, pins,
            lambda vm, address, count: words[address][:count])

    def test_incomplete_quiescence_rejects_releases(self):
        for fault in range(5):
            with self.subTest(fault=fault):
                names, words, log, values, live, pins = self.incomplete_fixture()
                if fault == 0: words[names["KERNEL_AP_LOT_RECORD"]][6] = 6
                elif fault == 1: words[names["KERNEL_AP_BATCH_PTE"]][1] = 0
                elif fault == 2: pins[0] = 0
                elif fault == 3: live[1] = 0
                else: words[names["KERNEL_AP_BATCH_RECORD"]][39] = 1
                with self.assertRaises(RuntimeError):
                    firmware.concurrent_batch.verify_incomplete(None, log, *values, live, pins,
                        lambda vm, address, count: words[address][:count])

    def test_stress_requires_higher_counts(self):
        from types import SimpleNamespace
        names, words, values = self.concurrent_fixture()
        log = SimpleNamespace(number=lambda name: names[name])
        read = lambda vm, address, count: words[address][:count]
        with self.assertRaises(RuntimeError):
            firmware.concurrent_batch.verify(None, log, 4, *values, read, 8, 8)
        for name in ("KERNEL_AP_BATCH_RECORD", "KERNEL_AP_SECOND_RECORD"):
            record = words[names[name]]
            record[18], record[27] = 16, 18
            words[record[33]][74] = 18 | 2 << 32
        for name in ("KERNEL_AP_PREEMPT_RECORD", "KERNEL_AP_SECOND_PREEMPT"):
            words[names[name]][1:6] = [8, 8, 8, 8, 16]
        firmware.concurrent_batch.verify(None, log, 4, *values, read, 8, 8)


if __name__ == "__main__":
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(RegisterParsing))
    raise SystemExit(not result.wasSuccessful())
