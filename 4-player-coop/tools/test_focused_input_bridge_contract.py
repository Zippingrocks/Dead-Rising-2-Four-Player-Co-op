import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).with_name("bridge-harness-focused-input.ps1")


class FocusedInputBridgeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8-sig")

    def test_requires_four_owned_private_input_processes(self):
        self.assertIn("Four distinct owned PIDs in session order are required", self.source)
        self.assertIn("-coopinstance=$instance", self.source)
        self.assertIn("-coopprivatemouse", self.source)
        self.assertIn("Fresh live samples for all four owned processes are required", self.source)

    def test_foreground_selects_exact_private_command_owner(self):
        self.assertIn("GetForegroundWindow()", self.source)
        self.assertIn("[Array]::IndexOf($windows, $foreground)", self.source)
        self.assertIn("Release-Instance $active", self.source)
        self.assertIn('"coop_input.$instance.txt"', self.source)
        self.assertIn('"coop_mouse.$instance.txt"', self.source)

    def test_controller_is_bounded_to_selected_instance(self):
        self.assertIn("XInputGetState", self.source)
        self.assertIn("ReadController(0", self.source)
        self.assertIn("LeftThumbY", self.source)
        self.assertIn("RightThumbX", self.source)
        self.assertIn("RightTrigger", self.source)

    def test_release_is_fail_closed(self):
        self.assertIn("GetAsyncKeyState(0x7B)", self.source)
        self.assertIn("foreach ($instance in 0..3) {", self.source)
        self.assertIn("try { Release-Instance $instance } catch {}", self.source)
        self.assertIn("Invoke-CommandFileRetry", self.source)
        self.assertIn("foreach ($attempt in 1..25)", self.source)
        self.assertIn("catch [IO.IOException]", self.source)
        self.assertIn("catch [UnauthorizedAccessException]", self.source)
        self.assertIn("Set-CommandText $keyboardPath ''", self.source)
        self.assertIn('("{0} 0 0 0 0" -f $script:sequence)', self.source)
        self.assertNotIn("WriteProcessMemory", self.source)
        self.assertNotIn("TerminateProcess", self.source)


if __name__ == "__main__":
    unittest.main()
