"""Read native item ownership and local player health; never mutate game state."""

import argparse
from datetime import datetime, timezone
import json
import math
import struct

from snapshot_campaign_players import capture_players
from snapshot_connection_mesh import ProcessReader


SIGNATURES = {
    0x45EE9B: bytes.fromhex('83fe03772b8bc6c1e0042bc6397cc3108d44c310751a85c074168b486885c97c0f83f90c7d0a8b44'),
    0x758190: bytes.fromhex('8b812cd80000c3'),
    0x7581A0: bytes.fromhex('8a813cd80000c3'),
    0x6BCF77: bytes.fromhex('a1a8e9dd008b4808d9412c'),
    0x81E471: bytes.fromhex('f30f5e857c440000'),
}


def capture_inventory(reader):
    if any(reader.read(site, len(code)) != code for site, code in SIGNATURES.items()):
        raise ValueError('Unsupported PC inventory/health code signatures')
    players = capture_players(reader)
    game, scene = int(players['game'], 16), int(players['scene'], 16)
    if not scene:
        raise ValueError('No active campaign scene')
    local = reader.u32(scene + 0x98)
    if local > 3 or not players['actors'][local] or players['actors'][local]['remote_enabled']:
        raise ValueError('Invalid local actor ownership')
    if players['actors'][local]['hidden'] or players['actors'][local]['render_hidden']:
        raise ValueError('Local campaign actor is still hidden/loading')
    managers = reader.u32(scene + 0x90)
    inventory = reader.u32(managers + 0x30)
    records = []
    for slot, actor in enumerate(players['actors']):
        record = inventory + 0x10 + slot * 0x78
        owner = reader.u32(record)
        items = []
        selected = struct.unpack('<i', reader.read(record + 0x68, 4))[0]
        if owner and (not actor or owner != int(actor['address'], 16)):
            raise ValueError('Inventory owner differs from native actor slot')
        if selected < -1 or selected >= 12:
            raise ValueError('Invalid selected inventory index')
        for index in range(12):
            prop = reader.u32(record + 4 + index * 8)
            item = None
            if prop:
                network = reader.u32(prop + 0xAC)
                if not network:
                    raise ValueError('Item has no native network identity')
                item = reader.u32(network + 0x40)
            items.append(item)
        records.append({'user': slot, 'actor': f'0x{owner:08X}', 'items': items,
                        'selected_index': selected,
                        'selected_item': items[selected] if selected >= 0 else None})
    actor = int(players['actors'][local]['address'], 16)
    status = reader.u32(actor + 0xD82C)
    if (reader.u32(status) != 0xC877A8 or reader.u32(status + 0x4408) != actor or
        reader.u32(0xC877A8 + 0x5C) != 0x6BCF10 or
        reader.u32(0xC9A720 + 0xBC) != 0x7581A0):
        raise ValueError('Unsupported native local-health ownership/accessors')
    tracker = reader.u32(0xDDE9A8)
    data = reader.u32(tracker + 8)
    health = struct.unpack('<f', reader.read(data + 0x2C, 4))[0]
    maximum = struct.unpack('<f', reader.read(status + 0x447C, 4))[0]
    if not all(math.isfinite(value) for value in (health, maximum)):
        raise ValueError('Non-finite health sample')
    if (reader.u32(game + 0x2C) != scene or reader.u32(scene + 0x90) != managers or
        reader.u32(managers + 0x30) != inventory or reader.u32(scene + 0x98) != local or
        reader.u32(actor + 0xD82C) != status or reader.u32(0xDDE9A8) != tracker or
        reader.u32(tracker + 8) != data):
        raise ValueError('Campaign owners changed during read')
    return {'local_user': local, 'local_health': health, 'local_maximum_health': maximum,
            'inventory': records, 'actors': players['actors'],
            'limitation': 'Non-atomic read-only sample; item/health values alone do not prove replication or input causality.'}


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
            sample = capture_inventory(reader)
            sample.update(pid=pid, captured_at=datetime.now(timezone.utc).isoformat())
            samples.append(sample)
        finally:
            reader.close()
    print(json.dumps(samples, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
