import unittest

from campaign_navigation import plan_step


class NavigationTests(unittest.TestCase):
    def camera(self):
        return {'local_actor': {'position': [0, 0, 0]},
                'ground_forward_xz': [0, 1], 'ground_right_xz': [-1, 0]}

    def test_each_native_direction_and_arrival(self):
        for x, z, key in ((0, 2, 17), (0, -2, 31), (2, 0, 30), (-2, 0, 32)):
            with self.subTest(key=key):
                self.assertEqual(plan_step(self.camera(), x, z)['scan_code'], key)
        self.assertTrue(plan_step(self.camera(), 0.1, 0.1)['arrived'])

    def test_rotated_camera_and_bounds(self):
        camera = self.camera()
        camera['ground_forward_xz'], camera['ground_right_xz'] = [1, 0], [0, 1]
        self.assertEqual(plan_step(camera, 2, 0)['scan_code'], 17)
        self.assertEqual(plan_step(camera, 0, 2)['scan_code'], 32)
        for x in (11, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                plan_step(camera, x, 0)
        self.assertEqual(plan_step(camera, 0.6, 0)['hold_ms'], 200)
