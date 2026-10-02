"""Choose a bounded ordinary movement key from a read-only native camera sample."""

import argparse
from datetime import datetime, timezone
import json
import math

from snapshot_campaign_camera import capture_camera
from snapshot_connection_mesh import ProcessReader


def plan_step(camera, target_x, target_z, tolerance=0.4):
    if not all(math.isfinite(v) for v in (target_x, target_z, tolerance)) or not 0.1 <= tolerance <= 0.5:
        raise ValueError('Invalid waypoint or tolerance')
    position = camera['local_actor']['position']
    delta = (target_x - position[0], target_z - position[2])
    distance = math.hypot(*delta)
    if distance > 10:
        raise ValueError('Waypoint exceeds the ten-unit local navigation bound')
    if distance <= tolerance:
        return {'arrived': True, 'distance': distance, 'scan_code': None, 'hold_ms': 0}
    forward, right = camera['ground_forward_xz'], camera['ground_right_xz']
    directions = ((17, forward), (31, [-v for v in forward]),
                  (32, right), (30, [-v for v in right]))
    key, direction = max(directions, key=lambda item: sum(a * b for a, b in zip(delta, item[1])))
    return {'arrived': False, 'distance': distance, 'scan_code': key,
            'hold_ms': 200 if distance < 0.8 else 300, 'direction_xz': direction}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--x', type=float, required=True)
    parser.add_argument('--z', type=float, required=True)
    args = parser.parse_args()
    if args.pid <= 0:
        parser.error('A positive owned PID is required')
    reader = ProcessReader(args.pid)
    try:
        camera = capture_camera(reader)
        camera.update(pid=args.pid, captured_at=datetime.now(timezone.utc).isoformat())
        print(json.dumps({'camera': camera, 'plan': plan_step(camera, args.x, args.z)}, allow_nan=False))
    finally:
        reader.close()


if __name__ == '__main__':
    main()
