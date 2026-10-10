import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[5]
spec = importlib.util.spec_from_file_location('active_console', ROOT / 'kernel/console/video/scripts/active.py')
active = importlib.util.module_from_spec(spec)
spec.loader.exec_module(active)

# ============ active campaign rejection cases ============

class ActiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / 'kernel/build', prefix='console-active-test-')
        self.directory = Path(self.temporary.name)
        self.boot = self.directory / 'boot-1'
        self.boot.mkdir()
        self.evidence = {'Image': 'ABC', 'Boots': 1, 'ItalianText': True, 'Shell': True,
            'StressKeys': 32, 'Results': [{'BudgetBefore': 10, 'BudgetAfter': 10,
                'PS2_TASK_IRQS': 1, 'PS2_IDLE': 2}]}
        self.log = '\n'.join(['KERNEL_FB_MAPPING_LIVE', 'KERNEL_PS2_TASK_POLL_READY',
            'KERNEL_PS2_INPUT_CLEANUP_OK', 'KERNEL_WATCHDOG_CLEANUP_OK',
            'KERNEL_CONSOLE_ACTIVE_RESOURCES_OK', 'KERNEL_READY'])

    def tearDown(self):
        self.temporary.cleanup()

    def verify(self):
        (self.directory / 'results.json').write_text(json.dumps(self.evidence), encoding='utf-8')
        (self.boot / 'serial.log').write_text(self.log, encoding='utf-8')
        return active.verify(self.directory, 'ABC', 1)

    def test_positive(self):
        self.assertEqual(self.verify(), self.evidence)

    def test_manifest_rejects(self):
        original = copy.deepcopy(self.evidence)
        for key, value in [('Image', 'BAD'), ('Boots', 2), ('ItalianText', False),
                ('Shell', False), ('StressKeys', 0), ('Results', [])]:
            self.evidence = copy.deepcopy(original)
            self.evidence[key] = value
            with self.assertRaises(RuntimeError): self.verify()

    def test_marker_order_and_fault(self):
        original = self.log
        for log in [original.replace('KERNEL_CONSOLE_ACTIVE_RESOURCES_OK', ''),
                original.replace('KERNEL_FB_MAPPING_LIVE', 'KERNEL_READY'),
                'PANIC\n' + original]:
            self.log = log
            with self.assertRaises(RuntimeError): self.verify()

    def test_accounting_irq_heartbeat(self):
        original = copy.deepcopy(self.evidence)
        for key, value in [('BudgetAfter', 9), ('PS2_TASK_IRQS', 0), ('PS2_IDLE', 1), ('BudgetBefore', 0)]:
            self.evidence = copy.deepcopy(original)
            self.evidence['Results'][0][key] = value
            with self.assertRaises(RuntimeError): self.verify()
        self.evidence = copy.deepcopy(original)
        del self.evidence['Results'][0]['BudgetBefore']
        del self.evidence['Results'][0]['BudgetAfter']
        with self.assertRaises(RuntimeError): self.verify()


if __name__ == '__main__':
    unittest.main()
