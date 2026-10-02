import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("show-harness-window-grid.ps1")


class HarnessWindowGridContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_requires_four_owned_live_harness_processes(self):
        self.assertIn("Four distinct owned PIDs in session order are required", self.source)
        self.assertIn("-coopinstance=$instance", self.source)
        self.assertIn("Fresh live samples for all four owned processes are required", self.source)

    def test_exposes_clickable_labeled_grid(self):
        self.assertIn("$WS_EX_NOACTIVATE", self.source)
        self.assertIn("$WS_EX_TOOLWINDOW", self.source)
        self.assertIn("$WS_EX_APPWINDOW", self.source)
        self.assertIn("$HWND_TOPMOST", self.source)
        self.assertIn("SetWindowPos", self.source)
        self.assertIn("RedrawWindow", self.source)
        self.assertIn('"DR2 Four-Player Test - Player $($instance + 1)"', self.source)

    def test_does_not_inject_or_reassign_gameplay(self):
        self.assertNotIn("WriteProcessMemory", self.source)
        self.assertNotIn("coop_input.", self.source)
        self.assertNotIn("coop_mouse.", self.source)
        self.assertNotIn("TerminateProcess", self.source)


if __name__ == "__main__":
    unittest.main()
