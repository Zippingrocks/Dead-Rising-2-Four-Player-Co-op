import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("bridge-harness-user-input.ps1")


class InteractiveInputBridgeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_bridge_requires_owned_isolated_process(self):
        self.assertIn("-coopinstance=$Instance", self.source)
        self.assertIn("-coopsilent", self.source)
        self.assertIn("-coopprivatemouse", self.source)
        self.assertIn("A fresh owned process sample is required", self.source)

    def test_bridge_only_drives_focused_owned_window(self):
        self.assertIn("GetForegroundWindow() -eq $window", self.source)
        self.assertIn("0x57=17", self.source)
        self.assertIn("0x41=30", self.source)
        self.assertIn("0x53=31", self.source)
        self.assertIn("0x44=32", self.source)
        self.assertIn("0x45=18", self.source)

    def test_bridge_has_release_and_cleanup(self):
        self.assertIn("GetAsyncKeyState(0x7B)", self.source)
        self.assertIn("release_key='F12'", self.source)
        self.assertIn("Remove-Item -LiteralPath $keyboardPath", self.source)
        self.assertIn("Remove-Item -LiteralPath $mousePath", self.source)
        self.assertNotIn("WriteProcessMemory", self.source)


if __name__ == "__main__":
    unittest.main()
