"""Regression for a register dump observed before its EFER line arrived."""
import importlib.util
import unittest
import sys
from pathlib import Path
from unittest.mock import patch
spec = importlib.util.spec_from_file_location("register_parent", Path(__file__).with_name("firmware.py"))
parent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parent)

# ====== RegistersTest ======

class RegistersTest(unittest.TestCase):
    def test_partial_reply_is_not_accepted(self):
        commands = []
        class Vm:
            def command(self, text):
                commands.append(text)
        prefix = "HLT=1\nCR0=80010033 CR4=00000620\n"
        def monitor(vm, command, ready):
            self.assertEqual(command, "info registers")
            self.assertIsNone(ready(prefix))
            self.assertIsNone(ready(prefix + "EFER=00000000"))
            full = prefix + "EFER=0000000000000d00\n"
            self.assertEqual(ready(full), full)
            return ready(full)
        with patch.object(parent.cpu, "monitor_read", monitor):
            self.assertIn("EFER=", parent.registers(Vm(), 2))
        self.assertEqual(commands, ["cpu 2", "cpu 0"])

    def test_failed_read_restores_selected_cpu(self):
        commands = []
        class Vm:
            def command(self, text):
                commands.append(text)
        with patch.object(parent.cpu, "monitor_read", side_effect=TimeoutError("incomplete")):
            with self.assertRaises(TimeoutError):
                parent.registers(Vm(), 3)
        self.assertEqual(commands, ["cpu 3", "cpu 0"])

if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
