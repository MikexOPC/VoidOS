import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('ap_console', Path(__file__).with_name('firmware.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

# ====== reader_fixture ======

class ReaderTests(unittest.TestCase):
    # ====== fixture ======

    def fixture(self, negative):
        self.record = [0x1000, 0x2000, 0x2000, 1, 1, 1000, 1000, 1000, 1000,
                       1 if negative else 3, 0 if negative else 1, int(negative), 100, 68, 0 if negative else 100]
        self.batch = [0x4000, 0x2000, 0x3000, 100, 68, 0 if negative else 100] + [0] * 26
        self.batch[20] = 2
        self.leaves = []
        for frame in (0x10000, 0x20000):
            self.leaves.extend([0] + [frame + 4096 * i | 3 | (1 << 63) for i in range(16)] + [0])
        self.pins = [1 | (1 << 32)]
        self.live = [0x10000, 16, 1, 0, 0x20000, 16, 1, 0]
        self.free = 68 if negative else 100
        self.order = True
        self.tables = {0x1000: self.record, 0x4000: self.batch, 0x5000: self.leaves}
        self.numbers = {'KERNEL_AP_CONSOLE_RECORD': 0x1000, 'KERNEL_AP_BATCH_RECORD': 0x4000,
                        'KERNEL_AP_BATCH_PTE': 0x5000}

    # ====== verify ======

    def verify(self, negative):
        log = type('Log', (), {})()
        log.number = lambda key: self.numbers[key]
        log.before = lambda first, second: self.order
        with patch.object(module.service.cpu, 'memory', side_effect=lambda vm, addr, count: self.tables[addr][:count]), \
             patch.object(module.service.launch, 'registers', return_value=''), \
             patch.object(module.service.launch, 'value', return_value=0x2000), \
             patch.object(module.service.runtime, 'allocator', side_effect=lambda vm, log: (0x3000, self.live, self.pins, self.free)):
            return module.verify_channel(None, log, negative)

    # ====== lifecycle_records ======

    def test_lifecycle_records(self):
        for negative in (False, True):
            self.fixture(negative)
            self.assertEqual(self.verify(negative)['Writes'], 2000)
            for index in range(15):
                self.fixture(negative)
                self.record[index] += 1
                with self.subTest(negative=negative, word=index), self.assertRaises(RuntimeError):
                    self.verify(negative)

    # ====== retained_memory ======

    def test_retained_memory(self):
        for mutation in ('guard', 'nx', 'pin', 'lease', 'collected', 'budget', 'order'):
            self.fixture(True)
            if mutation == 'guard': self.leaves[0] = 3
            if mutation == 'nx': self.leaves[1] &= ~(1 << 63)
            if mutation == 'pin': self.pins[0] = 1
            if mutation == 'lease': self.live[5] = 15
            if mutation == 'collected': self.batch[27] = 1
            if mutation == 'budget': self.free += 1
            if mutation == 'order': self.order = False
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError):
                self.verify(True)

if __name__ == '__main__':
    unittest.main()
