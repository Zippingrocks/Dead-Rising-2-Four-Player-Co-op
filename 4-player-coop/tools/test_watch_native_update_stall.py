import json
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from watch_native_update_stall import PlateauDetector, clock_ms, watch


class NativeUpdateStallWatcherTests(unittest.TestCase):
    def test_detects_repeated_nonzero_thread_and_call_count(self):
        detector = PlateauDetector(750)
        self.assertIsNone(detector.observe(
            "17:35:56.346 [1] host state transfer: health nativeUpdateThread=13992 nativeUpdateCalls=51150"))
        result = detector.observe(
            "17:35:57.360 [1] host state transfer: health nativeUpdateThread=13992 nativeUpdateCalls=51150")
        self.assertEqual(result["native_update_thread"], 13992)
        self.assertEqual(result["native_update_calls"], 51150)
        self.assertEqual(result["plateau_ms"], 1014)

    def test_progress_resets_plateau(self):
        detector = PlateauDetector(750)
        detector.observe(
            "17:35:56.000 [1] host state transfer: health nativeUpdateThread=7 nativeUpdateCalls=10")
        self.assertIsNone(detector.observe(
            "17:35:57.000 [1] host state transfer: health nativeUpdateThread=7 nativeUpdateCalls=11"))
        self.assertIsNone(detector.observe(
            "17:35:57.500 [1] host state transfer: health nativeUpdateThread=7 nativeUpdateCalls=11"))

    def test_ignores_unowned_or_incomplete_health_samples(self):
        detector = PlateauDetector(250)
        self.assertIsNone(detector.observe(
            "17:35:56.000 [1] host state transfer: health nativeUpdateThread=0 nativeUpdateCalls=10"))
        self.assertIsNone(detector.observe("17:35:57.000 unrelated"))

    def test_clock_handles_milliseconds(self):
        self.assertEqual(clock_ms("01:02:03.004"), 3723004)

    def test_watch_starts_at_eof_and_captures_first_fresh_plateau_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "coop_net.log"
            output = root / "evidence" / "threads.json"
            monitor = root / "monitor.exe"
            log.write_text(
                "17:00:00.000 health nativeUpdateThread=7 nativeUpdateCalls=10\n"
                "17:00:01.000 health nativeUpdateThread=7 nativeUpdateCalls=10\n",
                encoding="utf-8")

            def append_fresh_samples():
                time.sleep(0.05)
                with log.open("a", encoding="utf-8") as stream:
                    stream.write(
                        "17:00:02.000 health nativeUpdateThread=9 nativeUpdateCalls=20\n"
                        "17:00:02.300 health nativeUpdateThread=9 nativeUpdateCalls=20\n")
                    stream.flush()

            writer = threading.Thread(target=append_fresh_samples)
            writer.start()
            completed = subprocess.CompletedProcess([], 0, "captured", "")
            with patch("watch_native_update_stall.subprocess.run", return_value=completed) as run:
                result = watch(1234, log, output, monitor, threshold_ms=250,
                               poll_ms=20, timeout_seconds=2)
            writer.join()

            self.assertEqual(result["status"], "captured")
            self.assertEqual(result["evidence"]["native_update_thread"], 9)
            self.assertEqual(result["evidence"]["native_update_calls"], 20)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(
                run.call_args.args[0],
                [str(monitor), "--pid", "1234", "--threads-path", str(output)])
            sidecar = json.loads(
                output.with_suffix(".json.watch.json").read_text(encoding="utf-8"))
            self.assertEqual(sidecar["evidence"]["plateau_ms"], 300)

    def test_watch_refuses_to_overwrite_existing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "coop_net.log"
            output = root / "threads.json"
            log.write_text("", encoding="utf-8")
            output.write_text("existing", encoding="utf-8")
            with self.assertRaises(FileExistsError):
                watch(1, log, output, root / "monitor.exe", timeout_seconds=0)


if __name__ == "__main__":
    unittest.main()
