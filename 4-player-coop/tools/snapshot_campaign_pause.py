"""Observe native DR2 pause quorum without calling or modifying game code."""

import argparse
from datetime import datetime, timezone
import json
import struct

from snapshot_connection_mesh import ProcessReader


SIGNATURES = {
    0x853B00: bytes.fromhex('83b98800000004b801000000740f8b49288b497c85c97405e993ccffffc3'),
    0x8507B0: bytes.fromhex('8b5154568b720833c00b720c7405b8010000008b516c8b72080b720c740383c0018b91840000008b72080b720c5e740383c0018b899c0000008b51080b510c740383c001c3'),
    0x7A1F60: bytes.fromhex('33c08981dc0100008981e00100008981e40100008981e8010000c3'),
}


def capture_pause(reader):
    if reader.read(0x400000, 2) != b'MZ' or any(
        reader.read(site, len(code)) != code for site, code in SIGNATURES.items()
    ):
        raise ValueError('Unsupported native pause/count code signatures')
    game = reader.u32(0xDDC3F0)
    online = reader.u32(0xE5F428)
    state = reader.u32(game + 0x38) if game else 0
    p2p = reader.u32(online + 0xD8) if online else 0
    client = reader.u32(p2p + 0x38) if p2p else 0
    if not state or not client:
        raise ValueError('Native campaign state/client is not allocated')
    stage = reader.u32(client + 0x88)
    users = []
    user_list = 0
    count = 1
    if stage != 4:
        session = reader.u32(client + 0x28)
        user_list = reader.u32(session + 0x7C)
        if user_list:
            for offset in (0x54, 0x6C, 0x84, 0x9C):
                entry = reader.u32(user_list + offset)
                if not entry:
                    raise ValueError('Native user-list entry is null; sample is not valid')
                users.append(struct.unpack('<Q', reader.read(entry + 8, 8))[0])
            count = sum(value != 0 for value in users)
            occupied = [value for value in users if value]
            if len(occupied) != len(set(occupied)):
                raise ValueError('Aliased native user identities')
    masks = list(struct.unpack('<4I', reader.read(state + 0x1DC, 16)))
    if (reader.u32(0xDDC3F0) != game or reader.u32(game + 0x38) != state or
        reader.u32(0xE5F428) != online or reader.u32(online + 0xD8) != p2p or
        reader.u32(p2p + 0x38) != client or reader.u32(client + 0x88) != stage):
        raise ValueError('Native owners changed during the read; discard sample')
    return {'client_stage': stage, 'native_count': count,
            'users': [f'{value:016X}' for value in users], 'pause_masks': masks,
            'state': f'0x{state:08X}', 'client': f'0x{client:08X}',
            'limitation': 'Non-atomic read-only observation; zero masks alone do not prove usable gameplay.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, nargs='+', required=True)
    args = parser.parse_args()
    if len(set(args.pid)) != len(args.pid) or any(pid <= 0 for pid in args.pid):
        parser.error('Distinct positive PIDs are required')
    result = []
    for pid in args.pid:
        reader = ProcessReader(pid)
        try:
            sample = capture_pause(reader)
            sample.update(pid=pid, captured_at=datetime.now(timezone.utc).isoformat())
            result.append(sample)
        finally:
            reader.close()
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
