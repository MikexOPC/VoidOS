import importlib.util
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[5]
spec = importlib.util.spec_from_file_location('shell_pixels',
    ROOT / 'kernel/console/video/scripts/shell/pixels.py')
pixels = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pixels)

# ============ reference transcript boundaries ============

class TranscriptTests(unittest.TestCase):
    def test_input_and_response(self):
        log = 'PS2_LINE_READY: echo è €\n[ INFO ] SHELL_ECHO: è €\n'
        cells, caret = pixels.expected(log, 24, 4)
        self.assertEqual(caret, 50)
        self.assertEqual(''.join(map(chr, cells[:11])), '> echo è € ')
        self.assertEqual(''.join(map(chr, cells[24:40])), 'SHELL_ECHO: è € ')
        self.assertEqual(cells[48:51], [62, 32, 32])

    def test_scroll_and_deferred_wrap(self):
        cells, caret = pixels.expected('PS2_LINE_READY: A\nSHELL_ECHO: B\n', 3, 2)
        self.assertEqual(caret, 5)
        self.assertEqual(''.join(map(chr, cells)), 'B  >  ')
        self.assertEqual(pixels.expected('', 1, 1), ([32], 0))

    def test_ignore_debug_and_preserve_empty(self):
        log = '[ INFO ] SHELL_TASK2_READY\nPS2_TEXT: A\nPS2_LINE_READY: \n[ INFO ] SHELL_EMPTY\n'
        cells, caret = pixels.expected(log, 16, 3)
        self.assertEqual(caret, 34)
        self.assertEqual(cells[:2], [62, 32])
        self.assertEqual(''.join(map(chr, cells[16:27])), 'SHELL_EMPTY')
        self.assertEqual(cells[32:34], [62, 32])

    def test_invalid_lifetime_and_accounting(self):
        with self.assertRaisesRegex(RuntimeError, 'lifetime'):
            pixels.verify(None, None, '')
        log = 'KERNEL_CONSOLE_SHELL_OPEN\nKERNEL_CONSOLE_SHELL_CLEAN\n'
        for cells, groups, maximum in [(0, 1, 1), (9, 1, 1), (1, 2, 1), (1, 1, 0), (1, 1, 357956)]:
            sample = log + '\n'.join(f'KERNEL_CONSOLE_SHELL_{name}: 0x{value:016x}' for name, value in
                [('CELLS', cells), ('GROUPS', groups), ('MAX_PM', maximum), ('OVERRUNS', 0)])
            with self.assertRaisesRegex(RuntimeError, 'accounting'):
                pixels.verify(None, None, sample)

    def test_latency_accounting(self):
        for maximum, overruns, valid in [(357955, 0, True), (357956, 1, True),
            (357956, 0, False), (357955, 1, False), (357956, 3, False),
            (0x100000000, 1, False)]:
            sample = '\n'.join(f'KERNEL_CONSOLE_SHELL_{name}: 0x{value:016x}' for name, value in
                [('CELLS', 8), ('GROUPS', 2), ('MAX_PM', maximum), ('OVERRUNS', overruns)])
            if valid:
                self.assertEqual(pixels.accounting(sample), [8, 2, maximum, overruns])
            else:
                with self.assertRaisesRegex(RuntimeError, 'accounting'):
                    pixels.accounting(sample)
        with self.assertRaisesRegex(RuntimeError, 'accounting'):
            pixels.accounting('')


if __name__ == '__main__':
    unittest.main()
