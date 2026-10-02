import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("watch-harness-gameplay.py")


class GameplayWatcherContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_records_all_four_owned_instances(self):
        self.assertIn('nargs=4', self.source)
        self.assertIn('"instance": instance', self.source)
        self.assertIn('"interactive-gameplay-timeline.jsonl"', self.source)

    def test_records_gameplay_and_network_state(self):
        for capture in ("capture_players", "capture_combat", "capture_inventory",
                        "capture_pause", "capture_camera", "active.snapshot()"):
            self.assertIn(capture, self.source)

    def test_post_exit_index_waits_for_harness_evidence(self):
        self.assertIn('"network-summary.json"', self.source)
        self.assertIn('"dump-validation.json"', self.source)
        self.assertIn('"outcome.json"', self.source)
        self.assertIn('"interactive-gameplay-summary.json"', self.source)

    def test_observer_is_read_only(self):
        self.assertNotIn("WriteProcessMemory", self.source)
        self.assertNotIn("TerminateProcess", self.source)
        self.assertNotIn("SetWindowLong", self.source)


if __name__ == "__main__":
    unittest.main()
