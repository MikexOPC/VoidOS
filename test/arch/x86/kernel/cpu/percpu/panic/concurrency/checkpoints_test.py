"""A marker and its expected panic can appear between polling iterations."""
import importlib.util
import sys
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("panic_stress", Path(__file__).parents[1] / "integration/stress.py")
stress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stress)


# ====== VM ======

class VM:
    def __init__(self, samples, alive=True):
        self.samples = samples
        self.reads = 0
        self.running = alive

    def log(self):
        sample = self.samples[min(self.reads, len(self.samples) - 1)]
        self.reads += 1
        return stress.qemu.serial.SerialLog(sample)

    def alive(self):
        return self.running


# ====== Checkpoints ======

class Checkpoints(unittest.TestCase):
    def test_marker_and_panic_same_snapshot(self):
        for marker in ("KERNEL_AP_STRESS_IPI_ARMED", "KERNEL_AP_STRESS_LOCK_HELD"):
            with self.subTest(marker=marker):
                vm = VM(["", f"[  OK  ] {marker}\n[ FAIL ] KERNEL_SMP_PANIC\n"])
                result = stress.wait_checkpoint(vm, marker)
                self.assertTrue(result.has(marker, "OK"))
                self.assertEqual(vm.reads, 2)

    def test_panic_before_checkpoint_rejected(self):
        with self.assertRaises(RuntimeError):
            stress.wait_checkpoint(VM(["[ FAIL ] KERNEL_SMP_PANIC\n"]), "KERNEL_AP_STRESS_IPI_ARMED")

    def test_dead_vm_rejected(self):
        with self.assertRaises(RuntimeError):
            stress.wait_checkpoint(VM([""], alive=False), "KERNEL_AP_STRESS_IPI_ARMED")


if __name__ == "__main__":
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Checkpoints))
    raise SystemExit(0 if result.wasSuccessful() else 1)
