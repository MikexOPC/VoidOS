import contextlib
import importlib.util
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("runner_qemu", ROOT / "scripts/lib/qemu.py")
qemu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qemu)


# ====== BootReportTests ======

class BootReportTests(unittest.TestCase):
    # ====== render ======

    def render(self, results, passed, total, terminal=False, no_color=False):
        output = io.StringIO()
        environment = {"NO_COLOR": "1"} if no_color else {}
        with patch.dict(os.environ, environment, clear=True), patch.object(output, "isatty", return_value=terminal):
            with contextlib.redirect_stdout(output):
                qemu.report_boots(results, passed, total)
        return output.getvalue()

    # ====== test_passed_boots ======

    def test_passed_boots(self):
        text = self.render([{}, {}], 2, 2)
        self.assertEqual(text, "\n# ====== Boot 1 ====== #\n[ Pass ]\n"
                              "\n# ====== Boot 2 ====== #\n[ Pass ]\n")

    # ====== test_failure_stops_before_unexecuted_boots ======

    def test_failure_stops_before_unexecuted_boots(self):
        text = self.render([{}, {"Failure": "Missing KERNEL_READY\nserial.log"}], 1, 10)
        self.assertEqual(text.count("[ Pass ]"), 1)
        self.assertEqual(text.count("[ Fail ]"), 1)
        self.assertIn("Causa: Missing KERNEL_READY serial.log\n", text)
        self.assertIn("Boot non eseguiti: 8\n", text)
        self.assertNotIn("Boot 3", text)

    # ====== test_only_status_is_colored ======

    def test_only_status_is_colored(self):
        text = self.render([{}, {"Failure": "timeout"}], 1, 2, terminal=True)
        self.assertIn("# ====== Boot 1 ====== #\n\033[32m[ Pass ]\033[0m\n", text)
        self.assertIn("\033[31m[ Fail ]\033[0m\nCausa: timeout", text)
        self.assertEqual(text.count("\033["), 4)

    # ====== test_no_color_override ======

    def test_no_color_override(self):
        self.assertNotIn("\033[", self.render([{}], 1, 1, terminal=True, no_color=True))

    # ====== test_failure_detail_and_empty_message ======

    def test_failure_detail_and_empty_message(self):
        text = self.render([{"Failure": ""}], 0, 1)
        self.assertIn("[ Fail ]\nCausa: Errore senza messaggio", text)
        text = self.render([{}], 0, 1)
        self.assertIn("[ Fail ]\nCausa: Verifiche non superate", text)

    # ====== test_finish_preserves_json_and_result ======

    def test_finish_preserves_json_and_result(self):
        for passed, rows, expected in ((1, [{"ConfiguredVcpus": 4}], True),
                                       (0, [{"Failure": "timeout"}], False)):
            with self.subTest(passed=passed), tempfile.TemporaryDirectory(dir=ROOT / "kernel/build") as directory:
                output = io.StringIO()
                with patch.object(qemu, "image_hash", return_value="HASH"), contextlib.redirect_stdout(output):
                    self.assertEqual(qemu.finish(directory, "probe", "image.elf", rows, passed, 1), expected)
                report = json.loads((Path(directory) / "results.json").read_text())
                self.assertEqual(report, dict(Runner="probe", Image="HASH", Loader="HASH",
                                             Passed=passed, Total=1, Results=rows))
                self.assertIn(f"RESULT {'PASS' if expected else 'FAIL'} probe {passed}/1", output.getvalue())


if __name__ == "__main__":
    unittest.main()
