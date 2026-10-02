import unittest

from compare_campaign_motion import compare_motion


def samples(second):
    return [{'pid': 10 + i, 'game': 'game', 'scene': 'scene', 'manager': 'manager',
             'captured_at': f'2026-09-28T04:00:{second:02d}+00:00',
             'actors': [{'slot': slot, 'user': slot, 'address': str(slot + 100),
                         'remote_enabled': int(slot != i), 'position': [slot * 2.0, 0, 0],
                         'render_position': [slot * 2.0, 0, 0], 'hidden': False, 'render_hidden': False}
                        for slot in range(4)]} for i in range(4)]


class MotionComparisonTests(unittest.TestCase):
    def test_two_player_control_requires_explicit_capacity_and_hidden_unused_slots(self):
        before, after = samples(0)[:2], samples(2)[:2]
        for batch in (before, after):
            for record in batch:
                for actor in record['actors'][2:]:
                    actor.update(hidden=True, render_hidden=True)
        for record in after:
            record['actors'][1]['position'][0] += 1
        result = compare_motion(before, after, [10, 11], 1, players=2)
        self.assertTrue(result['isolated_motion_and_convergence_observed'])
        self.assertEqual(result['players'], 2)
        with self.assertRaises(ValueError):
            compare_motion(before, after, [10, 11], 1)
        with self.assertRaises(ValueError):
            compare_motion(before, after, [10, 11], 2, players=2)
        after[0]['actors'][2]['hidden'] = False
        with self.assertRaises(ValueError):
            compare_motion(before, after, [10, 11], 1, players=2)

    def test_two_player_control_keeps_the_same_error_limit(self):
        before, after = samples(0)[:2], samples(2)[:2]
        for batch in (before, after):
            for record in batch:
                record['actors'][2:] = [None, None]
        for record in after:
            record['actors'][1]['position'][0] += 1
        after[0]['actors'][1]['position'][0] += 0.201
        result = compare_motion(before, after, [10, 11], 1, players=2)
        self.assertFalse(result['isolated_motion_and_convergence_observed'])

    def test_each_individual_player_moves_and_converges_without_a_gameplay_claim(self):
        for player in range(4):
            before, after = samples(0), samples(2)
            for record in after:
                record['actors'][player]['position'][0] += 1
            result = compare_motion(before, list(reversed(after)), [10, 11, 12, 13], player)
            self.assertTrue(result['isolated_motion_and_convergence_observed'])
            self.assertNotIn('gameplay_verified', result)

    def test_stationary_divergent_and_mirrored_trials_do_not_pass(self):
        before, after = samples(0), samples(2)
        self.assertFalse(compare_motion(before, after, [10, 11, 12, 13], 0)['isolated_motion_and_convergence_observed'])
        after[0]['actors'][0]['position'][0] += 1
        self.assertFalse(compare_motion(before, after, [10, 11, 12, 13], 0)['isolated_motion_and_convergence_observed'])
        for record in after:
            for actor in record['actors']:
                actor['position'][0] += 1
        self.assertFalse(compare_motion(before, after, [10, 11, 12, 13], 0)['isolated_motion_and_convergence_observed'])

    def test_hidden_alias_nonfinite_and_wrong_owner_fail_closed(self):
        for key, value in [('hidden', True), ('render_hidden', True), ('address', '101'),
                           ('position', [float('nan'), 0, 0]), ('remote_enabled', 1), ('user', 3)]:
            after = samples(2)
            after[0]['actors'][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                compare_motion(samples(0), after, [10, 11, 12, 13], 0)

    def test_stale_pid_scene_and_time_are_rejected(self):
        for key, value in [('pid', 99), ('scene', 'new'), ('captured_at', '2026-09-28T04:00:00+00:00'),
                           ('captured_at', '2026-09-28T04:00:30+00:00')]:
            after = samples(2)
            after[0][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                compare_motion(samples(0), after, [10, 11, 12, 13], 0)
        with self.assertRaises(ValueError):
            compare_motion(samples(0), samples(2), [10, 10, 12, 13], 0)
