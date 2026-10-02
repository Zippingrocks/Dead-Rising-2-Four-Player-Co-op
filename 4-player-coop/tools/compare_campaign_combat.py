"""Compare two synchronized combat snapshots and report only directly observed deltas."""

import argparse
import json
import math


def load_samples(path, pids):
    with open(path, encoding='utf-8-sig') as stream:
        samples = json.load(stream)
    if not isinstance(samples, list) or len(samples) != len(pids):
        raise ValueError('Snapshot does not contain the expected process count')
    by_local = {}
    for sample in samples:
        local = sample.get('local_user')
        if local in by_local or not isinstance(local, int) or not 0 <= local < len(pids):
            raise ValueError('Snapshot local ownership is incomplete or aliased')
        if sample.get('pid') != pids[local]:
            raise ValueError('Snapshot PID does not match local ownership')
        players = sample.get('players')
        if not isinstance(players, list) or len(players) != len(pids) or any(player is None for player in players):
            raise ValueError('Snapshot does not contain every player status')
        for slot, player in enumerate(players):
            if player.get('slot') != slot:
                raise ValueError('Player status slot mismatch')
        by_local[local] = sample
    if set(by_local) != set(range(len(pids))):
        raise ValueError('Snapshot does not cover every local user')
    return by_local


def health_matrix(samples):
    return {
        observer: [float(player['effective_health']) for player in sample['players']]
        for observer, sample in samples.items()
    }


def compare(before, after, pids, tolerance=1.0):
    left = load_samples(before, pids)
    right = load_samples(after, pids)
    before_health = health_matrix(left)
    after_health = health_matrix(right)
    if any(not math.isfinite(value) for matrix in (before_health, after_health)
           for row in matrix.values() for value in row):
        raise ValueError('Non-finite health value')

    kill_deltas = [
        right[slot]['local_zombie_kills'] - left[slot]['local_zombie_kills']
        for slot in range(len(pids))
    ]
    if any(delta < 0 for delta in kill_deltas):
        raise ValueError('Zombie kill counter moved backwards')

    damaged_slots = []
    health_agreement = []
    ko_slots = []
    revived_slots = []
    for slot in range(len(pids)):
        deltas = [before_health[observer][slot] - after_health[observer][slot]
                  for observer in range(len(pids))]
        after_values = [after_health[observer][slot] for observer in range(len(pids))]
        agrees = max(after_values) - min(after_values) <= tolerance
        if agrees and all(delta >= 1.0 for delta in deltas):
            damaged_slots.append(slot)
        if agrees:
            health_agreement.append(slot)
        before_ready = [left[observer]['players'][slot]['ready_to_revive']
                        for observer in range(len(pids))]
        after_ready = [right[observer]['players'][slot]['ready_to_revive']
                       for observer in range(len(pids))]
        if all(after_ready) and max(after_values) <= tolerance:
            ko_slots.append(slot)
        if all(before_ready) and not any(after_ready) and min(after_values) > tolerance and agrees:
            revived_slots.append(slot)

    return {
        'pids': pids,
        'kill_deltas': kill_deltas,
        'kill_attributed_slots': [slot for slot, delta in enumerate(kill_deltas) if delta > 0],
        'damaged_slots': damaged_slots,
        'health_agreement_slots': health_agreement,
        'ko_slots': ko_slots,
        'revived_slots': revived_slots,
        'before_health_by_observer': before_health,
        'after_health_by_observer': after_health,
        'limitations': [
            'Kill attribution requires an owned attack between snapshots.',
            'Damage replication requires all observers to sample the same stable post-hit state.',
            'KO and revive are separate transitions and should be compared in separate snapshot pairs.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before')
    parser.add_argument('after')
    parser.add_argument('--pid', type=int, nargs='+', required=True)
    parser.add_argument('--tolerance', type=float, default=1.0)
    args = parser.parse_args()
    if len(args.pid) not in (2, 4) or len(set(args.pid)) != len(args.pid):
        parser.error('Two or four distinct PIDs are required')
    if not math.isfinite(args.tolerance) or args.tolerance < 0:
        parser.error('Tolerance must be finite and non-negative')
    print(json.dumps(compare(args.before, args.after, args.pid, args.tolerance), indent=2))


if __name__ == '__main__':
    main()
