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

    def verify(self, draw=False):
        (self.directory / 'results.json').write_text(json.dumps(self.evidence), encoding='utf-8')
        (self.boot / 'serial.log').write_text(self.log, encoding='utf-8')
        return active.verify(self.directory, 'ABC', 1, draw)

    def drawing(self):
        self.evidence['ConsoleDraw'] = True
        self.evidence['Results'][0]['ConsoleDraw'] = {'CELLS': 32, 'BATCH_SIZE': 1, 'GROUPS': 32, 'MAX_PM': 35796,
            'PM_LIMIT': 357955, 'PM_GOAL': 35796, 'SLOW_CELLS': 0, 'PM_MASK': 0xffffff, 'LAST_SCALAR': 0xe8,
            'LOSSES': 0, 'IRQS': 1, 'HPET_START': 10, 'HPET_END': 11,
            'PhysicalPixels': 162, 'VisiblePixels': 162}
        self.log = 'KERNEL_CONSOLE_DRAW_ACTIVE_OK\n' + self.log
        record = self.evidence['Results'][0]['ConsoleDraw']
        record['PROFILE_TOTAL'] = 192
        for phase in active.cell.PHASES:
            record[phase + '_SUM'] = 32
            record[phase + '_MAX'] = 1

    def test_measured_positive_and_profile(self):
        self.drawing()
        self.assertEqual(self.verify(True), self.evidence)
        record = self.evidence['Results'][0]['ConsoleDraw']
        record['MAX_PM'] = 118663; record['SLOW_CELLS'] = 8
        self.assertEqual(self.verify(True), self.evidence)
        with self.assertRaises(RuntimeError): self.verify()
        self.evidence['ConsoleDraw'] = False
        with self.assertRaises(RuntimeError): self.verify(True)
        self.evidence['ConsoleDraw'] = 1
        with self.assertRaises(RuntimeError): self.verify(True)

    def test_measured_rejections(self):
        self.drawing()
        original = copy.deepcopy(self.evidence)
        for key, value in [('CELLS', 31), ('CELLS', 65537), ('MAX_PM', 0),
                ('MAX_PM', 357956), ('MAX_PM', 35797), ('PM_LIMIT', 35795),
                ('PM_GOAL', 0), ('SLOW_CELLS', 1), ('SLOW_CELLS', 33), ('PM_MASK', 0xffff),
                ('LAST_SCALAR', 0x41), ('LOSSES', 1), ('IRQS', 0),
                ('HPET_END', 10), ('PhysicalPixels', 161), ('VisiblePixels', 161)]:
            self.evidence = copy.deepcopy(original)
            self.evidence['Results'][0]['ConsoleDraw'][key] = value
            with self.assertRaises(RuntimeError): self.verify(True)
        self.evidence = original
        self.log = self.log.replace('KERNEL_CONSOLE_DRAW_ACTIVE_OK', '')
        with self.assertRaises(RuntimeError): self.verify(True)

    def test_positive(self):
        self.assertEqual(self.verify(), self.evidence)

    def test_profile_rejections(self):
        self.drawing()
        original = copy.deepcopy(self.evidence)
        for key, value in [('PROFILE_TOTAL', 0), ('PROFILE_TOTAL', 193),
                ('PLAN_SUM', 33), ('PLAN_MAX', 0), ('PLAN_MAX', 35797), ('PLAN_SUM', True)]:
            self.evidence = copy.deepcopy(original)
            self.evidence['Results'][0]['ConsoleDraw'][key] = value
            with self.assertRaises(RuntimeError): self.verify(True)
        self.evidence = original
        del self.evidence['Results'][0]['ConsoleDraw']['GATE_SUM']
        with self.assertRaises(RuntimeError): self.verify(True)

    def test_batch_positive_and_rejections(self):
        self.drawing()
        record = self.evidence['Results'][0]['ConsoleDraw']
        record.update({'CELLS': 256, 'BATCH_SIZE': 8, 'GROUPS': 32, 'LAST_SCALAR': 0x41,
            'PhysicalPixels': 1296, 'VisiblePixels': 1296})
        self.assertEqual(self.verify(True), self.evidence)
        original = copy.deepcopy(self.evidence)
        for key, value in [('BATCH_SIZE', 0), ('BATCH_SIZE', 9), ('BATCH_SIZE', True),
                ('GROUPS', 31), ('GROUPS', True), ('CELLS', 255), ('PhysicalPixels', 162),
                ('VisiblePixels', 162), ('SLOW_CELLS', 1)]:
            self.evidence = copy.deepcopy(original)
            self.evidence['Results'][0]['ConsoleDraw'][key] = value
            with self.assertRaises(RuntimeError): self.verify(True)

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
                'PANIC\n' + original, 'KERNEL_CONSOLE_DRAW_REJECTED_PM\n' + original]:
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
