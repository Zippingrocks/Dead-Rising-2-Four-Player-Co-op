import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("follow-harness-leader.ps1")


class FormationFollowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_uses_owned_input_without_position_writes(self):
        self.assertIn("snapshot_campaign_camera.py", self.source)
        self.assertIn("campaign_navigation.py", self.source)
        self.assertIn("invoke-harness-input.ps1", self.source)
        self.assertIn("$input.ObserveNativeInput = $true", self.source)
        self.assertIn("$ownershipProven.ContainsKey($instance)", self.source)
        self.assertNotIn("WriteProcessMemory", self.source)

    def test_followers_stop_inside_proximity(self):
        self.assertIn("if ($distance -le 0.8) { continue }", self.source)
        self.assertIn("Behind=2.2; Side=-1.4", self.source)
        self.assertIn("Behind=2.2; Side=1.4", self.source)
        self.assertIn("Behind=3.8; Side=0.0", self.source)
        self.assertIn("if ($distance -gt 10.0) { 700 }", self.source)
        self.assertIn("elseif ($distance -gt 5.0) { 500 }", self.source)

    def test_scene_and_owner_changes_fail_closed(self):
        self.assertIn("$sample.camera.scene -ne $follower.scene", self.source)
        self.assertNotIn("$follower.scene -ne $leader.scene", self.source)
        self.assertIn("Follower ownership changed", self.source)
        self.assertIn("formation-follow.stop", self.source)


if __name__ == "__main__":
    unittest.main()
