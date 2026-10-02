"""Read native dynamic-prop identities and culling positions; never call or patch the game."""

import argparse
from datetime import datetime, timezone
import json
import math
import struct

from snapshot_campaign_players import capture_players
from snapshot_connection_mesh import ProcessReader


SIGNATURES = {
    0x4105E0: bytes.fromhex('8b4424043dff07000077078b448130c2040033c0c20400'),
    0x6140C5: bytes.fromhex('8b56048b82900000008b4820555333ede806c5dfff'),
    0x44EFA0: bytes.fromhex('80b9940000001f8d417472028b00c3'),
    0x865E36: bytes.fromhex('8b96ac0000008b4240'),
    0x48B1D8: bytes.fromhex('8b86ac0000008b5024'),
    0x48B1FF: bytes.fromhex('f30f105008f30f5c542414f30f104804f30f5c4c2410f30f1000f30f5c44240c'),
}


def read_name(reader, prop):
    address = prop + 0x74
    if reader.read(prop + 0x94, 1)[0] >= 31:
        address = reader.u32(address)
    if not address:
        raise ValueError('Missing prop name storage')
    data = bytearray()
    for offset in range(256):
        value = reader.read(address + offset, 1)[0]
        if value == 0:
            return data.decode('latin1')
        data.append(value)
    raise ValueError('Unterminated bounded prop name')


def capture_props(reader, item_ids=None):
    for address, signature in SIGNATURES.items():
        if reader.read(address, len(signature)) != signature:
            raise ValueError(f'Unsupported prop code at {address:08X}')
    players = capture_players(reader)
    game, scene = int(players['game'], 16), int(players['scene'], 16)
    if not scene:
        raise ValueError('No active scene')
    managers = reader.u32(scene + 0x90)
    environment = reader.u32(managers + 0x20) if managers else 0
    if not environment:
        raise ValueError('No native environment manager')
    table = reader.read(environment + 0x30, 2048 * 4)
    props = []
    for slot, address in enumerate(struct.unpack('<2048I', table)):
        if not address:
            continue
        info = reader.u32(address + 0xAC)
        if not info:
            raise ValueError(f'Prop slot {slot} has no native metadata')
        item_id = reader.u32(info + 0x40)
        if item_ids is not None and item_id not in item_ids:
            continue
        position = struct.unpack('<3f', reader.read(info, 12))
        if not all(math.isfinite(value) for value in position):
            raise ValueError('Non-finite native prop position')
        props.append({'slot': slot, 'address': f'0x{address:08X}', 'item_id': item_id,
                      'name': read_name(reader, address), 'position': position,
                      'metadata': f'0x{info:08X}', 'flags': f'0x{reader.u32(info + 0x24):08X}'})
        if reader.u32(address + 0xAC) != info:
            raise ValueError('Prop metadata changed during sampling')
    if (reader.u32(game + 0x2C) != scene or reader.u32(scene + 0x90) != managers or
            reader.u32(managers + 0x20) != environment or
            reader.read(environment + 0x30, len(table)) != table):
        raise ValueError('Scene or dynamic-prop table changed during sampling')
    return {'scene': players['scene'], 'environment': f'0x{environment:08X}', 'props': props,
            'limitation': 'Non-atomic culling-position metadata; not proof of current physics pose, reachability, pickup selection, or replicated ownership.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, nargs='+', required=True)
    parser.add_argument('--item-id', type=lambda value: int(value, 0), nargs='+')
    args = parser.parse_args()
    if any(pid <= 0 for pid in args.pid) or len(set(args.pid)) != len(args.pid):
        parser.error('Distinct positive PIDs required')
    if args.item_id and any(not 0 <= value <= 0xFFFFFFFF for value in args.item_id):
        parser.error('Item IDs must fit an unsigned 32-bit word')
    results = []
    for pid in args.pid:
        reader = ProcessReader(pid)
        try:
            result = capture_props(reader, None if args.item_id is None else set(args.item_id))
            result.update(pid=pid, captured_at=datetime.now(timezone.utc).isoformat())
            results.append(result)
        finally:
            reader.close()
    print(json.dumps(results, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
