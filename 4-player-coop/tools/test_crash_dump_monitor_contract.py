import pathlib
import unittest


SOURCE = (
    pathlib.Path(__file__).parents[2]
    / "tools"
    / "dr2-crash-dump-monitor"
    / "src"
    / "Program.cs"
)


class CrashDumpMonitorContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_wow64_debugger_control_exceptions_are_consumed_without_dumping(self):
        self.assertIn("STATUS_WX86_SINGLE_STEP = 0x4000001E", self.source)
        self.assertIn("STATUS_WX86_BREAKPOINT = 0x4000001F", self.source)
        self.assertIn("var shouldDump = !debuggerControl", self.source)
        self.assertIn("continueStatus = debuggerControl ? DBG_CONTINUE", self.source)


if __name__ == "__main__":
    unittest.main()
