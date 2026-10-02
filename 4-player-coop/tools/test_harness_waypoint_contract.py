import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("invoke-harness-waypoint.ps1")


class HarnessWaypointContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_waypoint_records_native_input_ownership(self):
        call = self.source.index("invoke-harness-input.ps1")
        sleep = self.source.index("Start-Sleep -Milliseconds 150", call)
        block = self.source[call:sleep]
        self.assertIn("-ObserveNativeInput", block)

    def test_waypoint_remains_bounded_and_observational(self):
        self.assertIn("[ValidateRange(1,30)][int]$MaximumSteps = 24", self.source)
        self.assertIn("Native camera/waypoint observation failed", self.source)
        self.assertNotIn("WriteProcessMemory", self.source)


if __name__ == "__main__":
    unittest.main()
