import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[5]
spec = importlib.util.spec_from_file_location('console_firmware', ROOT / 'kernel/console/video/scripts/firmware.py')
firmware = importlib.util.module_from_spec(spec)
spec.loader.exec_module(firmware)

# ============ fail-closed campaign validation ============

class CampaignTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / 'kernel/build', prefix='console-test-')
        self.directory = Path(self.temporary.name)
        if not self.directory.resolve().is_relative_to((ROOT / 'kernel/build').resolve()):
            raise RuntimeError('Fixture outside workspace build directory')
        self.addCleanup(self.temporary.cleanup)
        self.image = self.directory / 'kernel.elf'
        self.image.write_bytes(b'test fixture, not a boot image')
        self.evidence = {'Kernel': hashlib.sha256(self.image.read_bytes()).hexdigest().upper(),
            'LoaderUnchanged': True, 'Results': [{}]}
        (self.directory / 'boot-1').mkdir()

    def execute(self, diagnostic=False, marker=False, returncode=0):
        (self.directory / 'results.json').write_text(json.dumps(self.evidence), encoding='utf-8')
        (self.directory / 'boot-1/serial.log').write_text('KERNEL_CONSOLE_GRID_OK' if marker else '', encoding='utf-8')
        arguments = ['firmware.py', '--kernel', str(self.image), '--boots', '1']
        if diagnostic:
            arguments.append('--diagnostic')
        result = subprocess.CompletedProcess([], returncode, stdout='PASS: evidence ' + str(self.directory), stderr='')
        with patch.object(sys, 'argv', arguments), patch.object(firmware.subprocess, 'run', return_value=result) as run, \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            firmware.main()
            self.assertNotIn('timeout', run.call_args.kwargs)
            command = run.call_args.args[0]
            self.assertEqual(command[command.index('--shutdown-timeout') + 1], '15')

    def test_positive_and_marker_policy(self):
        self.execute()
        self.execute(diagnostic=True, marker=True)
        with self.assertRaises(RuntimeError):
            self.execute(diagnostic=True)
        with self.assertRaises(RuntimeError):
            self.execute(marker=True)

    def test_failed_provider_never_passes(self):
        with self.assertRaises(RuntimeError):
            self.execute(returncode=1)

    def test_corrupt_evidence_never_passes(self):
        for field, value in [('Kernel', 'wrong'), ('LoaderUnchanged', False), ('Results', []), ('Failure', 'failed')]:
            with self.subTest(field=field):
                original = dict(self.evidence)
                self.evidence[field] = value
                with self.assertRaises(RuntimeError):
                    self.execute()
                self.evidence = original

    def test_invalid_shutdown_deadline(self):
        for value in ['0', '31', 'nan']:
            with self.subTest(value=value), patch.object(sys, 'argv', ['firmware.py', '--kernel', str(self.image),
                    '--shutdown-timeout', value]), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    firmware.main()


if __name__ == '__main__':
    unittest.main()
