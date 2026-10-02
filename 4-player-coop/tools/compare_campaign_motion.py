"""Compare read-only campaign samples; input causality and rendered visibility remain separate checks."""

import argparse
from datetime import datetime
import json
import math
from pathlib import Path


def compare_motion(before, after, pids, player, minimum_movement=0.25, maximum_peer_error=0.2,
                   maximum_other_movement=0.15, maximum_sample_skew=1.0, players=4):
    limits = (minimum_movement, maximum_peer_error, maximum_other_movement, maximum_sample_skew)
    if players not in (2, 4) or len(pids) != players or len(set(pids)) != players or any(type(pid) is not int or pid <= 0 for pid in pids):
        raise ValueError('Distinct owned PIDs must match the explicit two/four-player capacity')
    if player not in range(players) or not all(math.isfinite(value) and value > 0 for value in limits):
        raise ValueError('Invalid player or comparison limits')
    batches = []
    for records in (before, after):
        if len(records) != players or {record['pid'] for record in records} != set(pids):
            raise ValueError('Sample PID set does not match the owned processes')
        ordered = [next(record for record in records if record['pid'] == pid) for pid in pids]
        times = [datetime.fromisoformat(record['captured_at']) for record in ordered]
        if any(time.tzinfo is None for time in times) or (max(times) - min(times)).total_seconds() > maximum_sample_skew:
            raise ValueError('Samples are not timestamped closely enough for comparison')
        for instance, record in enumerate(ordered):
            actors = record['actors']
            if len(actors) != 4 or any(actor is None for actor in actors[:players]):
                raise ValueError('All active actor records and four slot records are required')
            allocated = [actor for actor in actors if actor is not None]
            if len({actor['address'] for actor in allocated}) != len(allocated):
                raise ValueError('Actor pointers are aliased within a process')
            if any(actor is not None and (actor['hidden'] is not True or actor['render_hidden'] is not True)
                   for actor in actors[players:]):
                raise ValueError('Unexpected visible actor outside the declared player capacity')
            for slot, actor in enumerate(actors[:players]):
                if actor['slot'] != slot or actor['user'] != slot or actor['remote_enabled'] != int(slot != instance):
                    raise ValueError('Actor identity/ownership does not match the player slot')
                for key in ('position', 'render_position'):
                    values = actor[key]
                    if len(values) != 3 or not all(isinstance(v, (float, int)) and math.isfinite(v) for v in values):
                        raise ValueError('Invalid actor coordinates')
                if actor['hidden'] is not False or actor['render_hidden'] is not False:
                    raise ValueError('Hidden actor flags prevent an active-player motion check')
        batches.append((ordered, times))
    (old, old_times), (new, new_times) = batches
    for instance in range(players):
        if new_times[instance] <= old_times[instance]:
            raise ValueError('After samples must be newer than before samples')
        if any(old[instance][key] != new[instance][key] for key in ('game', 'scene', 'manager')):
            raise ValueError('Scene or manager changed during the movement trial')
        if any(old[instance]['actors'][slot]['address'] != new[instance]['actors'][slot]['address'] for slot in range(players)):
            raise ValueError('Actor identity changed during the movement trial')
    displacement = [math.dist(old[i]['actors'][i]['position'], new[i]['actors'][i]['position']) for i in range(players)]
    peer_errors = [[math.dist(new[i]['actors'][slot]['position'], new[slot]['actors'][slot]['position'])
                    for i in range(players)] for slot in range(players)]
    observed = (displacement[player] >= minimum_movement and
                all(value <= maximum_other_movement for i, value in enumerate(displacement) if i != player) and
                all(value <= maximum_peer_error for row in peer_errors for value in row))
    return {'player': player, 'players': players, 'pids': pids, 'owner_displacements': displacement,
            'peer_position_errors': peer_errors, 'isolated_motion_and_convergence_observed': observed,
            'limitation': 'Non-atomic positional evidence only. Requires matching fresh input acknowledgements, connected native state, rendered captures, and gameplay checks.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('--pid', type=int, nargs='+', required=True)
    parser.add_argument('--players', type=int, choices=(2, 4), default=4)
    parser.add_argument('--player', type=int, choices=range(4), required=True)
    args = parser.parse_args()
    try:
        result = compare_motion(json.loads(args.before.read_text(encoding='utf-8-sig')),
                                json.loads(args.after.read_text(encoding='utf-8-sig')), args.pid, args.player, players=args.players)
    except (ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
