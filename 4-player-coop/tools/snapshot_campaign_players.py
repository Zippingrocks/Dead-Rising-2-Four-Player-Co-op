"""Read four retail-PC human actor positions without writing memory or calling game code."""

import argparse
from datetime import datetime, timezone
import json
import math
import struct

from snapshot_connection_mesh import ProcessReader


POSITION_GETTER = bytes.fromhex('8b511c8b44240489108b51208b4924895004894808c20400')


def capture_players(reader):
    if reader.read(0x400000, 2) != b'MZ' or reader.read(0x7A1AB0, len(POSITION_GETTER)) != POSITION_GETTER:
        raise ValueError('Unsupported PC position getter; refusing guessed actor offsets')
    if reader.u32(0xC9A738) != 0x7A1AB0:
        raise ValueError('Unsupported human actor position vtable entry')
    game = reader.u32(0xDDC3F0)
    scene = reader.u32(game + 0x2C) if game else 0
    manager = reader.u32(scene + 0x94) if scene else 0
    actors = []
    seen = set()
    for slot in range(4):
        address = reader.u32(manager + 0xC + 4 * slot) if manager else 0
        if not address:
            actors.append(None)
            continue
        if address in seen:
            raise ValueError('Aliased human actor slots')
        seen.add(address)
        data = reader.read(address, 0x3A4)
        if struct.unpack_from('<I', data)[0] != 0xC9A720:
            raise ValueError(f'Unsupported human actor vtable in slot {slot}')
        position = struct.unpack_from('<3f', data, 0x1C)
        render_position = struct.unpack_from('<3f', data, 0x48)
        if not all(math.isfinite(value) for value in position + render_position):
            raise ValueError(f'Non-finite actor position in slot {slot}')
        actors.append({'slot': slot, 'address': f'0x{address:08X}',
                       'user': struct.unpack_from('<i', data, 0x3A0)[0],
                       'position': list(position), 'render_position': list(render_position),
                       'hidden': bool(data[0x18]), 'render_hidden': bool(data[0x44]),
                       'remote_enabled': reader.u8(address + 0xD83C)})
    if manager and (reader.u32(game + 0x2C) != scene or reader.u32(scene + 0x94) != manager):
        raise ValueError('Scene changed during the read; discard this sample')
    return {'game': f'0x{game:08X}', 'scene': f'0x{scene:08X}', 'manager': f'0x{manager:08X}',
            'actors': actors, 'limitation': 'Read-only non-atomic samples, not a visibility, control, or replication verdict.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, nargs='+', required=True)
    args = parser.parse_args()
    results = []
    for pid in args.pid:
        reader = ProcessReader(pid)
        try:
            result = capture_players(reader)
            result.update(pid=pid, captured_at=datetime.now(timezone.utc).isoformat())
            results.append(result)
        finally:
            reader.close()
    print(json.dumps(results, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
