import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("test-four-instances.ps1")


class AdmissionSeparationContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_separation_is_explicit_and_requires_confirmed_admission(self):
        self.assertIn("[switch]$SeparateConfirmedJoiners", self.source)
        self.assertIn(
            "if ($SeparateConfirmedJoiners -and -not $ConfirmCampaignAdmission)",
            self.source,
        )

    def test_joiner_moves_only_after_activation(self):
        confirmation = self.source.index("Wait-InstanceTrace 0 (\"client table:")
        separation = self.source.index("if ($SeparateConfirmedJoiners)", confirmation)
        activation = self.source.index("singlePlayer=0 gameType=1", separation)
        reverse_order = self.source.index(
            "foreach ($joiner in @($joiners | Sort-Object -Descending))",
            separation,
        )
        movement = self.source.index("Pulse-InstanceKeys @($joiner) $scanCode 1200", activation)
        self.assertLess(confirmation, separation)
        self.assertLess(separation, reverse_order)
        self.assertLess(reverse_order, activation)
        self.assertLess(activation, movement)

    def test_separation_records_provenance_without_memory_writes(self):
        start = self.source.index("if ($SeparateConfirmedJoiners)")
        end = self.source.index("                        }", start) + 25
        block = self.source[start:end]
        self.assertIn("Separate activated joiner after complete admission", block)
        self.assertNotIn("WriteProcessMemory", block)


if __name__ == "__main__":
    unittest.main()
