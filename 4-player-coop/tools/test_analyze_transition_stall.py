import json
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from analyze_transition_stall import analyze


class TransitionStallTests(unittest.TestCase):
    def _fixture(self, run):
        input_row = {"StartedAt": "2026-09-28T16:39:14.657-07:00", "Instance": 0,
                     "ScanCode": 18, "Completed": True, "Error": None}
        (run / "gameplay-input.jsonl").write_text(json.dumps(input_row) + "\n", encoding="utf-8")
        host = []
        for second in (15, 20, 25, 30, 35):
            host.append(f"16:39:{second:02d}.000 [1] host state transfer: health "
                        "nativeUpdateThread=77 nativeUpdateCalls=53037")
        host.append("16:39:20.500 [77] loader wait: tick=1000 site=009E7D5D scheduler=12340000 "
                    "ownerThread=77 schedulerState=1 pending=1 event=12345678 complete=0 "
                    "eventWords=00000000,00000000,00000000,00000000 thread=77")
        (run / "coop_net.log").write_text("\n".join(host) + "\n", encoding="utf-8")
        for instance in range(1, 4):
            client = [
                f"16:39:31.{instance}00 [2] mesh listener: topology periodic listener=A state=4 started=1",
                f"16:39:31.{instance}00 [2] direct client: reliable state vtable=A state=3 socket=B",
                f"16:39:31.{instance}00 [2] direct client: endpoint[0]=A "
                "peer=0110000170000000 previousState=6 state=8 ready=1",
            ]
            (run / f"coop_net.{instance}.log").write_text("\n".join(client) + "\n", encoding="utf-8")
        threads = {"threads": [{"capture": {"threadId": 77, "instruction": "ntdll.dll+0x7352C",
                    "stackAddressHints": [{"value": f"0x{address:08X}"} for address in
                                          (0x00A3A1C9, 0x009A692E, 0x009E7D5D)]}}]}
        (run / "threads-transition-stall.0.json").write_text(json.dumps(threads), encoding="utf-8")

    def test_detects_complete_pre_processflow_signature(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            self._fixture(run)
            result = analyze(run)
            self.assertTrue(result["detected"])
            self.assertEqual(result["classification"], "pre-processflow-loader-stall")
            self.assertEqual(result["host_native_update"]["distinct_call_counts"], [53037])
            self.assertLessEqual(result["client_timeout_skew_ms"], 2000)
            self.assertEqual(result["loader_wait_probe"]["completion_values"], [0])

    def test_processflow_or_missing_thread_signature_fails_closed(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            self._fixture(run)
            with (run / "coop_net.log").open("a", encoding="utf-8") as stream:
                stream.write("16:39:20.000 [1] native transition: ProcessFlow enter call=1\n")
            self.assertFalse(analyze(run)["detected"])
            (run / "coop_net.log").write_text(
                (run / "coop_net.log").read_text(encoding="utf-8").replace(
                    "native transition: ProcessFlow enter", "ignored"), encoding="utf-8")
            (run / "threads-transition-stall.0.json").unlink()
            self.assertFalse(analyze(run)["detected"])

    def test_allows_updates_before_the_final_frozen_plateau(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            self._fixture(run)
            host = (run / "coop_net.log").read_text(encoding="utf-8")
            host = host.replace("16:39:15.000 [1] host state transfer: health "
                                "nativeUpdateThread=77 nativeUpdateCalls=53037",
                                "16:39:15.000 [1] host state transfer: health "
                                "nativeUpdateThread=77 nativeUpdateCalls=53009")
            (run / "coop_net.log").write_text(host, encoding="utf-8")
            result = analyze(run)
            self.assertTrue(result["detected"])
            self.assertEqual(result["host_native_update"]["final_call_count"], 53037)
            self.assertGreaterEqual(result["host_native_update"]["trailing_plateau_span_ms"], 10000)


if __name__ == "__main__":
    unittest.main()
