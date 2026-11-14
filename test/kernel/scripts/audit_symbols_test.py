import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SHELL = os.environ.get('VOIDOS_PWSH') or shutil.which('pwsh') or 'pwsh'

# ====== map_audit ======

class AuditTests(unittest.TestCase):
    # ====== audit ======

    def audit(self, extra, *arguments, count=100):
        with tempfile.TemporaryDirectory(dir=ROOT / 'kernel/build') as directory:
            path = Path(directory) / 'kernel.map'
            rows = [f' 0001:{index:08x} c_symbol_{index} ffffffff80001000 f module.obj' for index in range(count)]
            path.write_text('\n'.join(rows + extra), encoding='utf-8')
            result = subprocess.run([str(SHELL), '-NoProfile', '-File', str(ROOT / 'kernel/scripts/audit-symbols.ps1'),
                                     '-Map', str(path), *arguments], capture_output=True, text=True,
                                    encoding='utf-8', errors='replace', timeout=20)
            return result.returncode, result.stdout + result.stderr

    # ====== rust_archive ======

    def test_rust_archive(self):
        row = [' 0001:00001000 rust_symbol ffffffff80002000 f console-requests:queue.cgu.0.rcgu.o']
        code, output = self.audit(row, '-Required', 'rust_symbol')
        self.assertEqual(code, 0, output)
        self.assertIn('101 symbols', output)
        code, output = self.audit(row, '-Forbidden', 'rust_symbol')
        self.assertNotEqual(code, 0)
        self.assertIn('forbidden symbol present: rust_symbol', output)

    # ====== negative_gates ======

    def test_negative_gates(self):
        row = [' 0001:00001000 diagnostic ffffffff80002000 f test-kernel-console-ap-channel.obj']
        code, output = self.audit(row)
        self.assertNotEqual(code, 0)
        self.assertIn('test object linked', output)
        code, output = self.audit(row, '-AllowTestObjects')
        self.assertEqual(code, 0, output)
        self.assertNotEqual(self.audit([], '-Required', 'absent_symbol')[0], 0)
        self.assertNotEqual(self.audit([], count=2)[0], 0)

if __name__ == '__main__':
    unittest.main()
