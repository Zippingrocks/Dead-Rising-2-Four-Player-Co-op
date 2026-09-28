"""Decode bounded native SYN traces; missing captured packets are not proof of packet loss."""

import argparse
import json
from pathlib import Path
import re
import struct


LOCAL_ID_BASE = 0x0110000170000000
HEADER = re.compile(
    r"mesh handshake: direction=(send|receive) peer=([0-9A-Fa-f]{16}) "
    r"channel=(\d+) bytes=(\d+) header=([0-9A-Fa-f]+)"
)
ADMISSION = re.compile(
    r"mesh admission: query=(\S+) (?:nonce|peerId)=([0-9A-Fa-f]{16}) "
    r"remoteAddress=([0-9A-Fa-f]{16}) port=(\d+) accepted=([01])"
)


def decode_syn(data):
    # Pack 00855440 / Unpack 00849320 establish big-endian stream fields after the prefix.
    # Steam-boundary captures precede socket unframing: do not label their first word game data size.
    if len(data) != 22:
        raise ValueError("Expected exactly 22 bytes for the captured SYN header")
    flags, errors, sequence, ack, port, offset, selective = struct.unpack_from(
        ">BBHHHHH", data, 2
    )
    if not flags & 1:
        raise ValueError("22-byte capture does not have the native SYN flag")
    if selective:
        raise ValueError("Selective ACK bytes are unsupported in a SYN-only capture")
    return {
        "transport_prefix_le16": struct.unpack_from("<H", data)[0],
        "flags": flags, "error_codes": errors,
        "sequence": sequence, "cumulative_ack": ack, "new_port": port,
        "unreliable_offset": offset, "selective_ack_count": selective,
        "peer_id": f"{struct.unpack_from('>Q', data, 14)[0]:016X}",
    }


def analyze(run, instances=4):
    packets, admissions, errors = [], [], []
    for instance in range(instances):
        path = run / ("coop_net.log" if instance == 0 else f"coop_net.{instance}.log")
        if not path.exists():
            continue
        with path.open(encoding="utf-8-sig", errors="replace") as stream:
            for number, line in enumerate(stream, 1):
                match = HEADER.search(line)
                if match:
                    direction, peer, channel, size, raw = match.groups()
                    source = f"{LOCAL_ID_BASE + instance:016X}" if direction == "send" else peer.upper()
                    destination = peer.upper() if direction == "send" else f"{LOCAL_ID_BASE + instance:016X}"
                    try:
                        data = bytes.fromhex(raw)
                        if int(size) != len(data) or int(channel) != 5679:
                            raise ValueError("Capture length/channel does not match the SYN trace contract")
                        decoded = decode_syn(data)
                        packets.append({
                            "file": path.name, "line": number, "instance": instance,
                            "direction": direction, "source": source, "destination": destination,
                            "header": raw.upper(), "decoded": decoded,
                            "identity_matches_transport_source": decoded["peer_id"] == source,
                        })
                    except ValueError as error:
                        errors.append(f"{path.name}:{number}: {error}")
                match = ADMISSION.search(line)
                if match:
                    query, peer, address, port, accepted = match.groups()
                    admissions.append({
                        "file": path.name, "line": number, "instance": instance, "query": query,
                        "peer_id": peer.upper(), "remote_address": address.upper(),
                        "port": int(port), "accepted": accepted == "1",
                    })
    received = {(p["source"], p["destination"], p["header"]) for p in packets if p["direction"] == "receive"}
    for packet in packets:
        if packet["direction"] == "send":
            packet["identical_receive_captured"] = (
                packet["source"], packet["destination"], packet["header"]
            ) in received
    return {
        "run": str(run.resolve()), "packets": packets, "admissions": admissions,
        "decode_errors": errors,
        "identity_mismatches": [p for p in packets if not p["identity_matches_transport_source"]],
        "limitation": "Bounded header capture; no missing-packet, mesh-completion, or gameplay inference.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--instances", type=int, choices=(2, 3, 4), default=4)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    text = json.dumps(analyze(args.run, args.instances), indent=2)
    if args.write:
        (args.run / "mesh-handshakes.json").write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
