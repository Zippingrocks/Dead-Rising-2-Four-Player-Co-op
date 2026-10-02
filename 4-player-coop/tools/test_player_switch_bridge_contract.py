import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("bridge-harness-player-switch.ps1")


class PlayerSwitchBridgeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_requires_four_owned_isolated_instances(self):
        self.assertIn("Four distinct owned PIDs in session order are required", self.source)
        self.assertIn("-coopinstance=$instance", self.source)
        self.assertIn("-coopsilent", self.source)
        self.assertIn("-coopprivatemouse", self.source)

    def test_f1_through_f4_switch_one_visible_owner(self):
        self.assertIn("GetAsyncKeyState(0x70 + $slot)", self.source)
        self.assertIn("$script:windows[$instance] = $candidate", self.source)
        self.assertIn("Set-VisibleInstance $active", self.source)
        self.assertIn("Release-Instance $active", self.source)
        self.assertIn("event='switched'", self.source)

    def test_switch_activates_camera_viewport_and_input_queue(self):
        self.assertIn("PostMessage($script:windows[$slot], 0x8435", self.source)
        self.assertIn("AttachThreadInput($foregroundThread, $targetThread, $true)", self.source)
        self.assertIn("SetForegroundWindow($script:windows[$slot])", self.source)
        self.assertIn("SetActiveWindow($script:windows[$slot])", self.source)
        self.assertIn("SetFocus($script:windows[$slot])", self.source)
        self.assertIn('"DR2 Four-Player Test - Player $($slot + 1)"', self.source)

    def test_f12_releases_all_owned_commands(self):
        self.assertIn("GetAsyncKeyState(0x7B)", self.source)
        self.assertIn("foreach ($instance in 0..3) { Release-Instance $instance }", self.source)
        self.assertNotIn("WriteProcessMemory", self.source)


if __name__ == "__main__":
    unittest.main()
