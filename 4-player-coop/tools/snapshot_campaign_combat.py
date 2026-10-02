"""Read native four-player combat state without mutating or calling game code."""

import argparse
from datetime import datetime, timezone
import json
import math
import struct

from snapshot_campaign_inventory import capture_inventory
from snapshot_connection_mesh import ProcessReader


# Retail PC sites that establish the decoded fields below. The kill routine
# increments cPlayerDataTracker::mZombiesKilled at +0x38; the other sites are
# the native health and co-op revival consumers.
SIGNATURES = {
    0x848977: bytes.fromhex('83463c018b7c241483463801'),
    0x6BCF62: bytes.fromhex('f30f1086c0090000f30f11442404'),
    0x4BEE71: bytes.fromhex('80bf10440000000f84fd000000'),
    0x4BEE92: bytes.fromhex('8bbf144400008b89'),
    0x81E726: bytes.fromhex('8bad1444000085ed7d0433c0eb0c'),
}


def _f32(reader, address):
    return struct.unpack('<f', reader.read(address, 4))[0]


def _i32(reader, address):
    return struct.unpack('<i', reader.read(address, 4))[0]


def capture_combat(reader):
    if any(reader.read(site, len(code)) != code for site, code in SIGNATURES.items()):
        raise ValueError('Unsupported PC combat/revival code signatures')

    base = capture_inventory(reader)
    local = base['local_user']
    tracker = reader.u32(0xDDE9A8)
    data = reader.u32(tracker + 8) if tracker else 0
    if not data:
        raise ValueError('No native player data tracker')

    zombie_kills = _i32(reader, data + 0x38)
    if zombie_kills < 0:
        raise ValueError('Invalid native zombie kill count')

    players = []
    for slot, actor_record in enumerate(base['actors']):
        if not actor_record:
            players.append(None)
            continue
        actor = int(actor_record['address'], 16)
        status = reader.u32(actor + 0xD82C)
        if (not status or reader.u32(status) != 0xC877A8 or
                reader.u32(status + 0x4408) != actor or
                _i32(reader, status + 0x440C) != slot):
            raise ValueError(f'Invalid native status ownership for player {slot}')

        status_health = _f32(reader, status + 0x9C0)
        maximum = _f32(reader, status + 0x447C)
        ready = reader.u8(status + 0x4410)
        revival = _i32(reader, status + 0x4414)
        if not math.isfinite(status_health) or not math.isfinite(maximum) or maximum <= 0:
            raise ValueError(f'Invalid native health state for player {slot}')
        if ready not in (0, 1) or not -1 <= revival <= 100:
            raise ValueError(f'Invalid native revival state for player {slot}')

        # The local accessor uses cPlayerDataTracker; remote actors use the
        # status copy. Record both and expose the value native gameplay reads.
        effective_health = base['local_health'] if slot == local else status_health
        players.append({
            'slot': slot,
            'actor': actor_record['address'],
            'status': f'0x{status:08X}',
            'effective_health': effective_health,
            'status_health': status_health,
            'maximum_health': maximum,
            'ready_to_revive': bool(ready),
            'revival_time_percent': revival,
            'remote_enabled': actor_record['remote_enabled'],
            'hidden': actor_record['hidden'],
            'render_hidden': actor_record['render_hidden'],
        })

    if (reader.u32(0xDDE9A8) != tracker or reader.u32(tracker + 8) != data):
        raise ValueError('Player data tracker changed during combat sample')
    for record in players:
        if record and reader.u32(int(record['actor'], 16) + 0xD82C) != int(record['status'], 16):
            raise ValueError('Actor status changed during combat sample')

    return {
        'local_user': local,
        'local_zombie_kills': zombie_kills,
        'local_health': base['local_health'],
        'local_maximum_health': base['local_maximum_health'],
        'players': players,
        'inventory': base['inventory'],
        'limitation': (
            'Read-only non-atomic state. A before/after delta plus owned input and '
            'cross-observer agreement is required for combat or replication proof.'
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, nargs='+', required=True)
    args = parser.parse_args()
    if len(set(args.pid)) != len(args.pid) or any(pid <= 0 for pid in args.pid):
        parser.error('Distinct positive PIDs are required')

    samples = []
    for pid in args.pid:
        reader = ProcessReader(pid)
        try:
            sample = capture_combat(reader)
            sample.update(pid=pid, captured_at=datetime.now(timezone.utc).isoformat())
            samples.append(sample)
        finally:
            reader.close()
    print(json.dumps(samples, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
