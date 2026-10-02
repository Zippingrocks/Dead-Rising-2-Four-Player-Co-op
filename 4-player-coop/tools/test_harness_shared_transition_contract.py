import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("test-harness-shared-transition.ps1")


class SharedTransitionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_requires_four_owned_hidden_children(self):
        self.assertIn("Four distinct owned PIDs in session order are required", self.source)
        self.assertIn("-coopsilent", self.source)
        self.assertIn("-coopinstance=$instance", self.source)
        self.assertIn("A fresh live observation of all four harness children is required", self.source)

    def test_uses_proven_two_interaction_route(self):
        first = self.source.index("Move-To 0 4.2 23.7")
        first_interaction = self.source.index("Send-Key 0 18 700", first)
        door_settle = self.source.index("Start-Sleep -Seconds 20", first_interaction)
        checkpoint = self.source.index("Save-Players 'transition-before.json'", door_settle)
        settle = self.source.index("Start-Sleep -Seconds $SettleSeconds", checkpoint)
        final_interaction = self.source.index("Send-Key 0 18 700", settle)
        self.assertLess(first, first_interaction)
        self.assertLess(first_interaction, door_settle)
        self.assertLess(door_settle, checkpoint)
        self.assertLess(checkpoint, settle)
        self.assertLess(settle, final_interaction)

    def test_fails_closed_and_creates_phase2_baseline(self):
        self.assertIn("if ($distance -lt 20) { return $false }", self.source)
        self.assertIn("All four owners did not complete the shared transition", self.source)
        self.assertIn("-Label 'phase2-baseline'", self.source)
        self.assertNotIn("WriteProcessMemory", self.source)


if __name__ == "__main__":
    unittest.main()
