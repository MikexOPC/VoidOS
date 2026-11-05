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
    def concurrent_fixture(tasks=4, target=3, rounds=1):
        root, pool, free, first = 0x1000, 0x2000, 1000, 0xffffffff80105000
        aps = tasks // 2
        mask = (1 << (aps + 1)) - 2
        irq, gen = 6 + 3 * tasks, 11 + 3 * tasks
        steps = gen + tasks
        local_at = steps + 2 + tasks
        retained = local_at + tasks + 1
        records = [0xffffffff80110000 + slot * 1024 for slot in range(aps)]
        preempt = [0xffffffff80115000 + slot * 256 for slot in range(aps)]
        names = dict(KERNEL_AP_BATCH_RECORD=records[0], KERNEL_AP_SECOND_RECORD=records[1],
                     KERNEL_AP_PREEMPT_RECORD=preempt[0], KERNEL_AP_SECOND_PREEMPT=preempt[1],
                     KERNEL_AP_BATCH_PTE=0xffffffff80116000, KERNEL_AP_LOT_RECORD=0xffffffff80117000,
                     KERNEL_AP_LOT_ARRIVED=0xffffffff80117400,
                     KERNEL_AP_CPU_RECORD=records, KERNEL_AP_CPU_PREEMPT=preempt)
        bases = [0xffff810000000000 + i * 18 * 4096 for i in range(tasks)]
        words = {names["KERNEL_AP_BATCH_PTE"]: [0] * (18 * tasks),
                 names["KERNEL_AP_LOT_RECORD"]: [0, root, 1, mask, mask, mask, mask],
                 names["KERNEL_AP_LOT_ARRIVED"]: [1] * aps}
        for slot in range(aps):
            lower, address = slot * 2, records[slot]
            record = [0] * (24 + 6 * tasks)
            record[irq:irq + 5] = [2 * rounds, 1 << lower, bases[lower] + 8192, lower, 1]
            local = 0xffffffff80118000 + slot * 1024
            record[steps:steps + 2] = [2 * target + 2, 2]
            record[local_at] = local
            record[retained + 2:retained + 6] = [0x202, 0xe1, 1, 0]
            if slot == 0:
                record[:6] = [address, root, pool, free, free - 16 * tasks, free]
                record[6:irq] = [16] * tasks + [136 * (i + 1) for i in range(tasks)] + [17 * (i + 1) for i in range(tasks)]
                record[gen:gen + tasks] = [1] * tasks
                record[steps + 2:local_at] = bases
                record[local_at + 1:retained] = [0xffffffff80120000 + i * 1024 for i in range(tasks)]
                record[retained:retained + 2] = [tasks, tasks]
            words[address] = record
            words[local] = [0] * 88
            words[local][74] = (2 * target + 2) | 2 << 32
            words[local][80] = 0xffffffff00000000
            words[local][82:84] = [local, first + slot * 448]
            address = preempt[slot]
            words[address] = [address, target, target, target, target, 2 * target, 1, 0] + [bases[lower + i] + 8192 for i in range(2)] * 2 + [0xffffffff80001000] * 2 + [1000, 0]
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
    def incomplete_fixture(tasks=4):
        from types import SimpleNamespace
        names, words, values = RegisterParsing.concurrent_fixture(tasks)
        record = words[names["KERNEL_AP_BATCH_RECORD"]]
        record[5] = 0
        retained = 14 + 6 * tasks
        record[retained:retained + 2] = [0, 0]
        words[names["KERNEL_AP_LOT_RECORD"]][6] &= ~(1 << (tasks // 2))
        live = []
        for task in range(tasks):
            frame = 0x400000 + task * 65536
            live += [frame, 16, task + 1, 0]
            words[names["KERNEL_AP_BATCH_PTE"]][task * 18 + 1:task * 18 + 17] = [
                (frame + page * 4096) | 3 | (1 << 63) for page in range(16)]
        log = SimpleNamespace(number=lambda name: names[name], before=lambda first, second: True)
        return names, words, log, (*values[:2], values[2] - 16 * tasks), live, [0x100000001] * (tasks // 2)

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

    def test_four_profile_requires_supervisor_cpu(self):
        import contextlib
        import io
        with patch.object(sys, "argv", ["firmware.py", "--image", "unused.elf", "--case", "ap-four", "--smp", "4"]), \
                patch.object(firmware.qemu, "Vm") as launch_vm, contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as failure:
                firmware.main()
            self.assertEqual(failure.exception.code, 2)
            launch_vm.assert_not_called()

    def test_four_evidence_checks_last_cpu(self):
        from types import SimpleNamespace
        names, words, values = self.concurrent_fixture(8, 8, 8)
        log = SimpleNamespace(number=lambda name: names[name], numbers=lambda name: names[name])
        read = lambda vm, address, count: words[address][:count]
        firmware.concurrent_batch.verify(None, log, 5, *values, read, 8, 8, 8)
        words[names["KERNEL_AP_CPU_RECORD"][3]][33] = 2
        with self.assertRaises(RuntimeError):
            firmware.concurrent_batch.verify(None, log, 5, *values, read, 8, 8, 8)

    def test_four_incomplete_checks_last_lease(self):
        names, words, log, values, live, pins = self.incomplete_fixture(8)
        read = lambda vm, address, count: words[address][:count]
        firmware.concurrent_batch.verify_incomplete(None, log, *values, live, pins, read, 8)
        pins[-1] = 0
        with self.assertRaises(RuntimeError):
            firmware.concurrent_batch.verify_incomplete(None, log, *values, live, pins, read, 8)


if __name__ == "__main__":
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(RegisterParsing))
    raise SystemExit(not result.wasSuccessful())
