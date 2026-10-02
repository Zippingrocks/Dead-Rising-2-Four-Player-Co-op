"""Continuously snapshot owned DR2 gameplay state and retain a post-exit evidence index."""

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import time

from snapshot_campaign_camera import capture_camera
from snapshot_campaign_combat import capture_combat
from snapshot_campaign_inventory import capture_inventory
from snapshot_campaign_pause import capture_pause
from snapshot_campaign_players import capture_players
from snapshot_connection_mesh import ProcessReader


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def process_alive(pid):
    try:
        handle = ProcessReader(pid)
        handle.close()
        return True
    except OSError:
        return False


def capture_one(pid):
    reader = ProcessReader(pid)
    try:
        result = {"pid": pid, "alive": True}
        captures = (
            ("players", capture_players),
            ("combat", capture_combat),
            ("inventory", capture_inventory),
            ("pause", capture_pause),
            ("camera", capture_camera),
            ("network", lambda active: active.snapshot()),
        )
        for name, capture in captures:
            try:
                result[name] = capture(reader)
            except Exception as exc:  # Preserve the other independent observers.
                result.setdefault("errors", {})[name] = f"{type(exc).__name__}: {exc}"
        return result
    finally:
        reader.close()


def load_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-root", type=Path, required=True)
    parser.add_argument("--pid", type=int, nargs=4, required=True)
    parser.add_argument("--interval-seconds", type=float, default=3.0)
    parser.add_argument("--duration-seconds", type=int, default=3600)
    args = parser.parse_args()
    run = args.run_root.resolve()
    if not run.is_dir() or "runtime_logs" not in {part.lower() for part in run.parts}:
        parser.error("run-root must be an existing workspace runtime_logs directory")
    if len(set(args.pid)) != 4 or any(pid <= 0 for pid in args.pid):
        parser.error("four distinct positive owned PIDs are required")
    if not 0.5 <= args.interval_seconds <= 30 or not 10 <= args.duration_seconds <= 7200:
        parser.error("invalid watcher interval or duration")

    timeline_path = run / "interactive-gameplay-timeline.jsonl"
    summary_path = run / "interactive-gameplay-summary.json"
    stop_path = run / "interactive-gameplay-watcher.stop"
    errors = Counter()
    samples = 0
    first_exit = None
    started = utc_now()
    deadline = time.monotonic() + args.duration_seconds

    with timeline_path.open("a", encoding="utf-8", buffering=1) as timeline:
        timeline.write(json.dumps({"event": "watcher-started", "captured_at": started,
                                   "pids": args.pid, "interval_seconds": args.interval_seconds}) + "\n")
        while time.monotonic() < deadline and not stop_path.exists():
            captured_at = utc_now()
            records = []
            alive_count = 0
            for instance, pid in enumerate(args.pid):
                try:
                    record = capture_one(pid)
                    alive_count += 1
                except Exception as exc:
                    record = {"pid": pid, "alive": False,
                              "error": f"{type(exc).__name__}: {exc}"}
                    errors[f"instance-{instance}:{type(exc).__name__}"] += 1
                    if first_exit is None:
                        first_exit = {"instance": instance, "pid": pid, "captured_at": captured_at,
                                      "error": record["error"]}
                record["instance"] = instance
                for category in record.get("errors", {}):
                    errors[f"instance-{instance}:{category}"] += 1
                records.append(record)
            timeline.write(json.dumps({"event": "sample", "captured_at": captured_at,
                                       "sequence": samples, "instances": records},
                                      separators=(",", ":"), allow_nan=False) + "\n")
            samples += 1
            if alive_count == 0:
                break
            time.sleep(args.interval_seconds)

        ended = utc_now()
        timeline.write(json.dumps({"event": "watcher-stopped", "captured_at": ended,
                                   "samples": samples, "first_exit": first_exit}) + "\n")

    # The parent harness validates dumps, copies native logs and writes its network summary after exit.
    if first_exit is not None:
        post_deadline = time.monotonic() + 240
        while time.monotonic() < post_deadline:
            if (run / "network-summary.json").exists() and (run / "outcome.json").exists():
                break
            time.sleep(2)

    evidence = {
        "watcher": "completed",
        "started_at": started,
        "ended_at": ended,
        "pids": args.pid,
        "samples": samples,
        "first_exit": first_exit,
        "observer_errors": dict(errors),
        "timeline": str(timeline_path),
        "outcome": load_json(run / "outcome.json"),
        "dump_validation": load_json(run / "dump-validation.json"),
        "network_summary_present": (run / "network-summary.json").exists(),
        "native_logs": sorted(path.name for path in run.glob("coop_net*.log")),
        "runtime_logs": sorted(path.name for path in run.glob("case_zero_runtime*.log")),
        "debugger_directories": sorted(path.name for path in run.glob("debugger.*") if path.is_dir()),
    }
    summary_path.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
