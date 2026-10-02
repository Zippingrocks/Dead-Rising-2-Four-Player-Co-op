"""Read the retail PC main camera and local actor without writing game state."""

import argparse
from datetime import datetime, timezone
import json
import math
import struct

from snapshot_campaign_players import capture_players
from snapshot_connection_mesh import ProcessReader


# Matched retail constructors/getter, not unverified OTR layout offsets.
SIGNATURES = {
    0x7A0CA0: bytes.fromhex('8b44240483f81177098b49308b0481c2040033c0c20400'),
    0x89487C: bytes.fromhex('8d4d18c74500403ecc00894508895d0ce8ffcc1700'),
    0x8949E2: bytes.fromhex('558bc8e8465deeffeb0233c06a02536a0368c00000008985d8000000'),
    0xA11590: bytes.fromhex('8bd1e879ffffffd9eec702dca2c300'),
    0xA11510: bytes.fromhex('d9ee8bc1c700f869c300d95004d9500856d9500c57'),
    0x77A7FF: bytes.fromhex('8b74240889700c'),
}


def capture_camera(reader):
    for address, signature in SIGNATURES.items():
        if reader.read(address, len(signature)) != signature:
            raise ValueError(f'Unsupported camera code at {address:08X}')
    players = capture_players(reader)
    game, scene = int(players['game'], 16), int(players['scene'], 16)
    local = reader.u32(scene + 0x98)
    if local not in range(4):
        raise ValueError('Invalid local user')
    actor = players['actors'][local]
    if not actor or actor['hidden'] or actor['render_hidden'] or actor['remote_enabled'] != 0:
        raise ValueError('Local actor is hidden or remotely owned')
    viewports = reader.u32(game + 0x30)
    viewport = reader.u32(viewports + 14 * 4) if viewports else 0
    if not viewport or reader.u32(viewport) != 0xCC3E40 or reader.u32(viewport + 8) != 14:
        raise ValueError('Unsupported main viewport')
    camera = viewport + 0x18
    manager = reader.u32(viewport + 0xD8)
    if reader.u32(camera) != 0xC3A2DC or not manager or reader.u32(manager) != 0xCD8CC4:
        raise ValueError('Unsupported camera objects')
    if reader.u32(manager + 0xC) != viewport:
        raise ValueError('Camera manager ownership mismatch')
    values = struct.unpack('<12f', reader.read(camera + 4, 48))
    if not all(math.isfinite(value) for value in values):
        raise ValueError('Non-finite camera state')
    position, view, up = values[:3], values[3:6], values[6:9]
    fov, near, far = values[9:]
    if (abs(math.dist(view, (0, 0, 0)) - 1) > 0.02 or
            abs(math.dist(up, (0, 0, 0)) - 1) > 0.02 or
            abs(sum(a * b for a, b in zip(view, up))) > 0.02 or
            not 1 < fov < 179 or not 0 < near < far):
        raise ValueError('Invalid camera basis or projection')
    horizontal_length = math.hypot(view[0], view[2])
    if horizontal_length < 0.05:
        raise ValueError('Camera is too vertical for planar input guidance')
    forward = [view[0] / horizontal_length, view[2] / horizontal_length]
    if (reader.u32(game + 0x2C) != scene or reader.u32(game + 0x30) != viewports or
            reader.u32(viewports + 14 * 4) != viewport or reader.u32(viewport + 0xD8) != manager or
            reader.u32(scene + 0x98) != local):
        raise ValueError('Camera/scene ownership changed during sampling')
    return {'game': players['game'], 'scene': players['scene'], 'local_user': local,
            'local_actor': actor, 'viewport': f'0x{viewport:08X}', 'camera': f'0x{camera:08X}',
            'manager': f'0x{manager:08X}', 'position': position, 'view': view, 'up': up,
            'fov_degrees': fov, 'near': near, 'far': far,
            # DR2 uses view cross up: live D input with +X view moves toward +Z.
            'ground_forward_xz': forward, 'ground_right_xz': [-forward[1], forward[0]],
            'limitation': 'Read-only non-atomic camera sample; movement mappings still require native input verification.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, nargs='+', required=True)
    args = parser.parse_args()
    if any(pid <= 0 for pid in args.pid) or len(set(args.pid)) != len(args.pid):
        parser.error('Positive distinct PIDs are required')
    results = []
    for pid in args.pid:
        reader = ProcessReader(pid)
        try:
            result = capture_camera(reader)
            result.update(pid=pid, captured_at=datetime.now(timezone.utc).isoformat())
            results.append(result)
        finally:
            reader.close()
    print(json.dumps(results, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
