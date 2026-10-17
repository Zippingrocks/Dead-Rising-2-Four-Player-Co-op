import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("watch-production-dr2.ps1")


class ProductionDr2GuardianContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8")

    def test_targets_exact_game_executable_and_rechecks_identity_before_kill(self):
        self.assertIn("[IO.Path]::GetFullPath($candidate.Path)", self.source)
        self.assertIn("Test-Identity $Process $StartTicks", self.source)
        self.assertIn("Stop-Process -Id $Process.Id -Force", self.source)
        self.assertNotIn("Stop-Process -Name", self.source)
        self.assertNotIn("taskkill", self.source.lower())

    def test_default_watch_is_fast_but_requires_sustained_failure(self):
        self.assertIn("[int]$PollMilliseconds = 100", self.source)
        self.assertIn("[int]$HangMilliseconds = 1500", self.source)
        self.assertIn("$unresponsiveAt.ElapsedMilliseconds -ge $HangMilliseconds", self.source)
        self.assertIn("WindowResponds($target.MainWindowHandle, 100)", self.source)

    def test_emergency_chord_and_bounded_evidence_precede_recovery(self):
        self.assertIn("Ctrl+Shift+F12", self.source)
        self.assertIn("EmergencyChordDown", self.source)
        self.assertIn("WaitForExit($EvidenceMilliseconds)", self.source)
        self.assertLess(self.source.index("'hang-confirmed'"), self.source.index("'sustained-window-hang'"))

    def test_crash_monitor_is_scoped_to_observed_pid(self):
        self.assertIn("@('--pid', $target.Id, '--out-dir', (Quote-NativeArgument $runDir)", self.source)
        self.assertIn("'--max-dumps', '6'", self.source)


if __name__ == "__main__":
    unittest.main()
