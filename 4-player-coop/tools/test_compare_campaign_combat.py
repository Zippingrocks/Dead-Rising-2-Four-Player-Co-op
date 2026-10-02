import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from compare_campaign_combat import compare


PIDS = [101, 102, 103, 104]


def samples(kills=(10, 20, 30, 40), health=(400, 400, 400, 400), ready=None):
    ready = ready or (False, False, False, False)
    return [
        {
            'pid': PIDS[observer],
            'local_user': observer,
            'local_zombie_kills': kills[observer],
            'players': [
                {'slot': slot, 'effective_health': float(health[slot]),
                 'ready_to_revive': ready[slot]}
                for slot in range(4)
            ],
        }
        for observer in range(4)
    ]


class CombatComparisonTests(unittest.TestCase):
    def run_compare(self, before, after):
        with TemporaryDirectory() as directory:
            left = Path(directory) / 'before.json'
            right = Path(directory) / 'after.json'
            left.write_text(json.dumps(before), encoding='utf-8')
            right.write_text(json.dumps(after), encoding='utf-8')
            return compare(left, right, PIDS)

    def test_kill_and_replicated_damage(self):
        result = self.run_compare(samples(), samples(kills=(10, 21, 30, 40), health=(400, 350, 400, 400)))
        self.assertEqual(result['kill_attributed_slots'], [1])
        self.assertEqual(result['damaged_slots'], [1])
        self.assertEqual(result['health_agreement_slots'], [0, 1, 2, 3])

    def test_ko_and_revive_transitions(self):
        ko = self.run_compare(samples(), samples(health=(400, 400, 0, 400), ready=(False, False, True, False)))
        self.assertEqual(ko['ko_slots'], [2])
        revived = self.run_compare(
            samples(health=(400, 400, 0, 400), ready=(False, False, True, False)),
            samples(health=(400, 400, 100, 400)))
        self.assertEqual(revived['revived_slots'], [2])

    def test_aliases_backwards_counts_and_partial_status_fail(self):
        bad = samples()
        bad[3]['local_user'] = 2
        with self.assertRaisesRegex(ValueError, 'aliased'):
            self.run_compare(samples(), bad)
        with self.assertRaisesRegex(ValueError, 'backwards'):
            self.run_compare(samples(), samples(kills=(9, 20, 30, 40)))
        bad = samples()
        bad[0]['players'][3] = None
        with self.assertRaisesRegex(ValueError, 'every player'):
            self.run_compare(samples(), bad)


if __name__ == '__main__':
    unittest.main()
