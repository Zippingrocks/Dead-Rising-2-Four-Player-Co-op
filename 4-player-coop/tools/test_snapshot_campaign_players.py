import struct
import unittest

from snapshot_campaign_players import POSITION_GETTER, capture_players
from test_snapshot_connection_mesh import MemoryFixture


class PlayerSnapshotTests(unittest.TestCase):
    def setUp(self):
        self.reader = MemoryFixture()
        self.manager = bytearray(0x20)
        game, scene = bytearray(0x30), bytearray(0x98)
        struct.pack_into('<I', game, 0x2C, 0x2000)
        struct.pack_into('<I', scene, 0x94, 0x3000)
        self.reader.blocks.update({0x400000: b'MZ', 0x7A1AB0: POSITION_GETTER,
            0xC9A738: struct.pack('<I', 0x7A1AB0), 0xDDC3F0: struct.pack('<I', 0x1000),
            0x1000: game, 0x2000: scene, 0x3000: self.manager})
        self.actors = []
        for slot in range(4):
            address = 0x10000 * (slot + 1)
            actor = bytearray(0xD840)
            struct.pack_into('<I', actor, 0, 0xC9A720)
            struct.pack_into('<3f', actor, 0x1C, slot * 10, 2, 3)
            struct.pack_into('<3f', actor, 0x48, slot * 10 + 1, 2, 3)
            struct.pack_into('<i', actor, 0x3A0, slot)
            actor[0xD83C] = slot != 0
            struct.pack_into('<I', self.manager, 0xC + slot * 4, address)
            self.reader.blocks[address] = actor
            self.actors.append(actor)

    def test_four_distinct_slots_keep_simulation_and_render_positions_separate(self):
        result = capture_players(self.reader)
        self.assertEqual([a['user'] for a in result['actors']], [0, 1, 2, 3])
        self.assertEqual(result['actors'][3]['position'], [30, 2, 3])
        self.assertEqual(result['actors'][3]['render_position'], [31, 2, 3])
        self.assertNotIn('replication_verified', result)

    def test_absent_scene_is_not_four_players(self):
        self.reader.blocks[0xDDC3F0] = bytes(4)
        self.assertEqual(capture_players(self.reader)['actors'], [None] * 4)

    def test_wrong_code_and_vtable_fail_closed(self):
        for address, bad in ((0x7A1AB0, bytes(len(POSITION_GETTER))), (0xC9A738, bytes(4))):
            original = self.reader.blocks[address]
            self.reader.blocks[address] = bad
            with self.assertRaises(ValueError):
                capture_players(self.reader)
            self.reader.blocks[address] = original
        struct.pack_into('<I', self.actors[0], 0, 0xDEADBEEF)
        with self.assertRaisesRegex(ValueError, 'vtable'):
            capture_players(self.reader)

    def test_aliases_and_nonfinite_positions_are_rejected(self):
        struct.pack_into('<I', self.manager, 0x10, 0x10000)
        with self.assertRaisesRegex(ValueError, 'Aliased'):
            capture_players(self.reader)
        struct.pack_into('<I', self.manager, 0x10, 0x20000)
        struct.pack_into('<f', self.actors[0], 0x1C, float('nan'))
        with self.assertRaisesRegex(ValueError, 'Non-finite'):
            capture_players(self.reader)


if __name__ == '__main__':
    unittest.main()
