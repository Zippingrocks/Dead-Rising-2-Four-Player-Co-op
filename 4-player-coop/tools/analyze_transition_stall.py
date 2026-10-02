"""Classify the four-instance pre-ProcessFlow transition stall from saved evidence."""

import argparse
import json
import re
from datetime import datetime
from pathlib import Path


LOG_CLOCK = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3})")
HOST_HEALTH = re.compile(r"nativeUpdateThread=(\d+) nativeUpdateCalls=(\d+)")
ENDPOINT = re.compile(r"peer=([0-9A-Fa-f]{16}).*? state=(\d+)")
LOADER_WAIT = re.compile(
    r"loader wait: tick=(\d+) site=([0-9A-Fa-f]+) scheduler=([0-9A-Fa-f]+) "
    r"ownerThread=(\d+) schedulerState=(-?\d+) pending=(-?\d+) event=([0-9A-Fa-f]+) "
    r"complete=(\d+)"
)

MAIN_WAIT_RETURNS = {0x00A3A1C9, 0x009A692E, 0x009E7D5D}


def _clock_ms(text):
    match = LOG_CLOCK.match(text)
    if not match:
        return None
    hour, minute, second, millisecond = map(int, match.groups())
    return ((hour * 60 + minute) * 60 + second) * 1000 + millisecond


def _after(clock, trigger):
    return clock is not None and trigger is not None and clock >= trigger


def _trigger(run):
    path = run / "gameplay-input.jsonl"
    candidates = []
    if path.is_file():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if (row.get("Instance") == 0 and row.get("ScanCode") == 18 and
                    row.get("Completed") is True and not row.get("Error")):
                try:
                    candidates.append(datetime.fromisoformat(row["StartedAt"]))
                except (KeyError, TypeError, ValueError):
                    pass
    if not candidates:
        return None, None
    selected = max(candidates)
    milliseconds = ((selected.hour * 60 + selected.minute) * 60 + selected.second) * 1000
    milliseconds += selected.microsecond // 1000
    return milliseconds, selected.isoformat()


def _lines(path):
    if not path.is_file():
        return []
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def _thread_wait(run, thread_id):
    path = run / "threads-transition-stall.0.json"
    result = {"file": path.name, "present": path.is_file(), "thread_id": thread_id,
              "instruction": None, "matched_returns": [], "verified": False}
    if not path.is_file() or thread_id is None:
        return result
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return result
    for row in document.get("threads", []):
        capture = row.get("capture") or {}
        if capture.get("threadId") != thread_id:
            continue
        hints = capture.get("stackAddressHints") or []
        addresses = set()
        for hint in hints:
            try:
                addresses.add(int(hint.get("value", ""), 0))
            except (TypeError, ValueError):
                pass
        matched = sorted(addresses & MAIN_WAIT_RETURNS)
        result.update({"instruction": capture.get("instruction"),
                       "matched_returns": [f"0x{address:08X}" for address in matched],
                       "verified": len(matched) == len(MAIN_WAIT_RETURNS)})
        break
    return result


def analyze(run):
    run = Path(run)
    trigger, trigger_iso = _trigger(run)
    host_lines = _lines(run / "coop_net.log")
    update_samples = []
    process_flow = []
    loader_wait_samples = []
    host_thread = None
    for line in host_lines:
        clock = _clock_ms(line)
        if not _after(clock, trigger):
            continue
        health = HOST_HEALTH.search(line)
        if health:
            host_thread = int(health.group(1))
            update_samples.append((clock, int(health.group(2))))
        if "native transition: ProcessFlow enter" in line:
            process_flow.append(line.strip())
        wait = LOADER_WAIT.search(line)
        if wait:
            tick, site, scheduler, owner, state, pending, event, complete = wait.groups()
            loader_wait_samples.append({"clock_ms": clock, "tick": int(tick), "site": site.upper(),
                                        "scheduler": scheduler.upper(), "owner_thread": int(owner),
                                        "scheduler_state": int(state), "pending": int(pending),
                                        "event": event.upper(), "complete": int(complete)})

    update_counts = sorted({count for _, count in update_samples})
    update_span_ms = (update_samples[-1][0] - update_samples[0][0]) if len(update_samples) > 1 else 0
    plateau_start = len(update_samples) - 1
    if update_samples:
        final_count = update_samples[-1][1]
        while plateau_start > 0 and update_samples[plateau_start - 1][1] == final_count:
            plateau_start -= 1
    plateau_span_ms = (update_samples[-1][0] - update_samples[plateau_start][0]) if update_samples else 0
    host_update_frozen = len(update_samples) > 1 and plateau_span_ms >= 10000

    client_results = []
    host_peer = "0110000170000000"
    state8_clocks = []
    for instance in range(1, 4):
        lines = _lines(run / f"coop_net.{instance}.log")
        state8 = listener4 = reliable3 = None
        for line in lines:
            clock = _clock_ms(line)
            if not _after(clock, trigger):
                continue
            endpoint = ENDPOINT.search(line)
            if endpoint and endpoint.group(1).upper() == host_peer and int(endpoint.group(2)) == 8:
                state8 = state8 or clock
            if "mesh listener: topology periodic" in line and " state=4 " in line:
                listener4 = listener4 or clock
            if "direct client: reliable state" in line and " state=3 " in line:
                reliable3 = reliable3 or clock
        if state8 is not None:
            state8_clocks.append(state8)
        client_results.append({"instance": instance, "host_endpoint_state8_ms": state8,
                               "listener_state4_ms": listener4, "reliable_state3_ms": reliable3,
                               "verified": all(value is not None for value in (state8, listener4, reliable3))})

    client_timeout_skew_ms = max(state8_clocks) - min(state8_clocks) if len(state8_clocks) == 3 else None
    simultaneous_client_timeout = (len(state8_clocks) == 3 and client_timeout_skew_ms <= 2000 and
                                   all(row["verified"] for row in client_results))
    thread_wait = _thread_wait(run, host_thread)
    detected = bool(trigger is not None and host_update_frozen and not process_flow and
                    simultaneous_client_timeout and thread_wait["verified"])
    return {
        "classification": "pre-processflow-loader-stall" if detected else "not-proven",
        "detected": detected,
        "trigger": {"source": "latest completed host E input", "iso": trigger_iso,
                    "clock_ms": trigger},
        "host_native_update": {"thread_id": host_thread, "sample_count": len(update_samples),
                               "distinct_call_counts": update_counts, "span_ms": update_span_ms,
                               "final_call_count": update_samples[-1][1] if update_samples else None,
                               "trailing_plateau_span_ms": plateau_span_ms,
                               "frozen": host_update_frozen},
        "process_flow_after_trigger": process_flow[:16],
        "loader_wait_probe": {"sample_count": len(loader_wait_samples),
                              "first": loader_wait_samples[0] if loader_wait_samples else None,
                              "last": loader_wait_samples[-1] if loader_wait_samples else None,
                              "events": sorted({row["event"] for row in loader_wait_samples}),
                              "completion_values": sorted({row["complete"] for row in loader_wait_samples}),
                              "pending_values": sorted({row["pending"] for row in loader_wait_samples})},
        "clients": client_results,
        "client_timeout_skew_ms": client_timeout_skew_ms,
        "simultaneous_client_timeout": simultaneous_client_timeout,
        "host_thread_wait": thread_wait,
        "interpretation": ("The game thread stopped in the pre-flow loader wait before ProcessFlow; "
                           "client link timeout followed." if detected else
                           "Saved evidence does not prove the full pre-flow loader-stall signature."),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    result = analyze(args.run)
    text = json.dumps(result, indent=2)
    if args.write:
        (args.run / "transition-stall-analysis.json").write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
