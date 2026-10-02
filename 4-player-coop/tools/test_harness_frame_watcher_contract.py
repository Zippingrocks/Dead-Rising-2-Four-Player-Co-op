import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("watch-harness-frames.ps1")


class HarnessFrameWatcherContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_requires_four_owned_pids_and_bounded_interval(self):
        self.assertIn("Four distinct owned PIDs in session order are required", self.source)
        self.assertIn("[ValidateRange(5,60)]", self.source)
        self.assertIn("Get-Process -Id $OwnedPids[$instance]", self.source)

    def test_requests_all_four_native_frame_captures(self):
        self.assertIn('"coop_capture.$_.flag"', self.source)
        self.assertIn("for ($instance = 0; $instance -lt 4; $instance++)", self.source)
        self.assertIn("capture-requested", self.source)

    def test_removes_only_exact_owned_flags(self):
        self.assertIn("foreach ($flag in $flags)", self.source)
        self.assertIn("Remove-Item -LiteralPath $flag", self.source)
        self.assertNotIn("WriteProcessMemory", self.source)
        self.assertNotIn("Stop-Process", self.source)


if __name__ == "__main__":
    unittest.main()
