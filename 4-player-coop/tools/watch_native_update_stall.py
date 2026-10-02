#!/usr/bin/env python3
"""Capture a host thread snapshot when native networking updates stop advancing."""

import argparse
import json
import re
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


HEALTH_RE = re.compile(
    r"^(?P<clock>\d{2}:\d{2}:\d{2}\.\d{3}).*?"
    r"nativeUpdateThread=(?P<thread>\d+) nativeUpdateCalls=(?P<calls>\d+)"
)


def clock_ms(value):
    hours, minutes, rest = value.split(":")
    seconds, milliseconds = rest.split(".")
    return (((int(hours) * 60 + int(minutes)) * 60 + int(seconds)) * 1000
            + int(milliseconds))


@dataclass
class PlateauDetector:
    threshold_ms: int
    calls: int | None = None
    first_clock_ms: int | None = None
    first_line: str | None = None

    def observe(self, line):
        match = HEALTH_RE.search(line)
        if not match or int(match.group("thread")) == 0:
            return None
        current_calls = int(match.group("calls"))
        current_clock = clock_ms(match.group("clock"))
        if self.calls != current_calls:
            self.calls = current_calls
            self.first_clock_ms = current_clock
            self.first_line = line.rstrip()
            return None
        elapsed = current_clock - self.first_clock_ms
        if elapsed < 0:
            elapsed += 24 * 60 * 60 * 1000
        if elapsed < self.threshold_ms:
            return None
        return {
            "native_update_thread": int(match.group("thread")),
            "native_update_calls": current_calls,
            "plateau_ms": elapsed,
            "first_line": self.first_line,
            "trigger_line": line.rstrip(),
        }


def watch(pid, log_path, output_path, monitor_path, threshold_ms=750,
          poll_ms=100, timeout_seconds=1800):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    sidecar = output_path.with_suffix(output_path.suffix + ".watch.json")
    if output_path.exists() or sidecar.exists():
        raise FileExistsError(f"refusing to overwrite stall evidence: {output_path}")
    detector = PlateauDetector(threshold_ms)
    started = time.monotonic()
    offset = log_path.stat().st_size if log_path.exists() else 0
    while time.monotonic() - started < timeout_seconds:
        if not log_path.exists():
            time.sleep(poll_ms / 1000)
            continue
        size = log_path.stat().st_size
        if size < offset:
            offset = 0
            detector = PlateauDetector(threshold_ms)
        with log_path.open("r", encoding="utf-8", errors="replace") as stream:
            stream.seek(offset)
            for line in stream:
                evidence = detector.observe(line)
                if evidence:
                    command = [str(monitor_path), "--pid", str(pid),
                               "--threads-path", str(output_path)]
                    completed = subprocess.run(command, capture_output=True, text=True,
                                               timeout=30, check=False)
                    result = {
                        "status": "captured" if completed.returncode == 0 else "capture-failed",
                        "pid": pid,
                        "evidence": evidence,
                        "monitor_exit_code": completed.returncode,
                        "monitor_stdout": completed.stdout.strip(),
                        "monitor_stderr": completed.stderr.strip(),
                    }
                    sidecar.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
                    return result
            offset = stream.tell()
        time.sleep(poll_ms / 1000)
    result = {"status": "timeout", "pid": pid, "timeout_seconds": timeout_seconds}
    sidecar.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(
        description="Capture host threads after a bounded native-update plateau.")
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--monitor", required=True, type=Path)
    parser.add_argument("--threshold-ms", type=int, default=750)
    parser.add_argument("--poll-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=1800)
    args = parser.parse_args()
    if args.pid <= 0 or args.threshold_ms < 250 or args.poll_ms < 20:
        parser.error("invalid PID or timing bound")
    result = watch(args.pid, args.log, args.output, args.monitor,
                   args.threshold_ms, args.poll_ms, args.timeout_seconds)
    print(json.dumps(result, indent=2))
    return 0 if result["status"] in ("captured", "timeout") else 1


if __name__ == "__main__":
    raise SystemExit(main())
