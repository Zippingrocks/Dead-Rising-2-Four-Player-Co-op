"""Observe retail PC logical button records without injecting or changing state."""

import argparse
from datetime import datetime, timezone
import json
import math
import struct
import time

from snapshot_campaign_players import capture_players
from snapshot_connection_mesh import ProcessReader


SIGNATURES = {
    0x44C780: bytes.fromhex('8b5424048b827803000083f8ff750532c0c20c0083f80f77f66bc05f535657'),
    0x44C7AE: bytes.fromhex('8d34c5000000002bf00f2f058c65c3008a9cb1480400008d34b1'),
    0x492050: bytes.fromhex('568b742408578bf98b47048b48546a016a0d56e818a7fbff84'),
}
BUTTON_COUNT = 95


def capture_input(reader):
    for address, signature in SIGNATURES.items():
        if reader.read(address, len(signature)) != signature:
            raise ValueError(f'Unsupported logical input code at {address:08X}')
    players = capture_players(reader)
    game, scene = int(players['game'], 16), int(players['scene'], 16)
    local = reader.u32(scene + 0x98)
    if local not in range(4):
        raise ValueError('Invalid local user')
    actor = players['actors'][local]
    if not actor or actor['hidden'] or actor['render_hidden'] or actor['remote_enabled'] != 0:
        raise ValueError('Local actor is hidden or remotely owned')
    address = int(actor['address'], 16)
    interface = reader.u32(scene + 0x54)
    input_index = reader.u32(address + 0x378)
    if not interface or input_index not in range(16):
        raise ValueError('Invalid native player input ownership')
    base = interface + 0x448 + input_index * BUTTON_COUNT * 28
    records = reader.read(base, BUTTON_COUNT * 28)
    buttons = []
    for index in range(BUTTON_COUNT):
        record = records[index * 28:(index + 1) * 28]
        flags = [record[offset] for offset in (0, 1, 2, 24)]
        timers = struct.unpack_from('<5f', record, 4)
        if any(value not in (0, 1) for value in flags) or not all(map(math.isfinite, timers)):
            raise ValueError(f'Invalid logical button record {index}')
        buttons.append({'index': index, 'state': bool(flags[0]), 'cache_next_tick': bool(flags[1]),
                        'using_cached': bool(flags[2]), 'requires_release': bool(flags[3]),
                        'timers': timers})
    if (reader.u32(game + 0x2C) != scene or reader.u32(scene + 0x54) != interface or
            reader.u32(scene + 0x98) != local or reader.u32(address + 0x378) != input_index or
            reader.u32(int(players['manager'], 16) + 0xC + 4 * local) != address):
        raise ValueError('Input ownership changed during sampling')
    return {'scene': players['scene'], 'local_user': local, 'actor': actor['address'],
            'interface': f'0x{interface:08X}', 'input_index': input_index,
            'active_indices': [button['index'] for button in buttons if button['state']],
            'buttons': buttons,
            'limitation': 'Non-atomic raw PC button records; numeric IDs are not OTR enum labels or proof of a combat action.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--samples', type=int, choices=range(1, 11), default=1)
    args = parser.parse_args()
    if args.pid <= 0:
        parser.error('A positive PID is required')
    reader = ProcessReader(args.pid)
    results = []
    try:
        for index in range(args.samples):
            sample = capture_input(reader)
            sample.update(pid=args.pid, captured_at=datetime.now(timezone.utc).isoformat())
            results.append(sample)
            if index + 1 < args.samples:
                time.sleep(0.02)
    finally:
        reader.close()
    print(json.dumps(results, allow_nan=False))


if __name__ == '__main__':
    main()
