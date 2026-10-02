import struct
import unittest
from unittest.mock import patch

from snapshot_campaign_combat import SIGNATURES, capture_combat


class Reader:
    def __init__(self):
        self.memory = dict(SIGNATURES)
        self.actors = [0x10000 + slot * 0x10000 for slot in range(4)]
        self.inventory = {
            'local_user': 2,
            'local_health': 275.0,
            'local_maximum_health': 400.0,
            'actors': [
                {'address': f'0x{actor:08X}', 'remote_enabled': int(slot != 2),
                 'hidden': False, 'render_hidden': False}
                for slot, actor in enumerate(self.actors)
            ],
            'inventory': [{'user': slot, 'items': [None] * 12} for slot in range(4)],
        }
        self.put(0xDDE9A8, 0x90000)
        self.put(0x90008, 0xA0000)
        self.put(0xA0038, 123)
        for slot, actor in enumerate(self.actors):
            status = 0xB0000 + slot * 0x10000
            self.put(actor + 0xD82C, status)
            self.put(status, 0xC877A8)
            self.put(status + 0x4408, actor)
            self.s32(status + 0x440C, slot)
            self.memory[status + 0x9C0] = struct.pack('<f', 300.0 + slot)
            self.memory[status + 0x447C] = struct.pack('<f', 400.0)
            self.memory[status + 0x4410] = bytes([1 if slot == 3 else 0])
            self.s32(status + 0x4414, 42 if slot == 3 else 0)

    def put(self, address, value):
        self.memory[address] = struct.pack('<I', value)

    def s32(self, address, value):
        self.memory[address] = struct.pack('<i', value)

    def read(self, address, size):
        data = self.memory.get(address, bytes(size))
        if len(data) != size:
            raise AssertionError('Unexpected read size')
        return data

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def u8(self, address):
        return self.read(address, 1)[0]


class CombatSnapshotTests(unittest.TestCase):
    def capture(self, reader):
        with patch('snapshot_campaign_combat.capture_inventory', return_value=reader.inventory):
            return capture_combat(reader)

    def test_kills_health_and_revival_are_owned(self):
        sample = self.capture(Reader())
        self.assertEqual(sample['local_user'], 2)
        self.assertEqual(sample['local_zombie_kills'], 123)
        self.assertEqual(sample['players'][2]['effective_health'], 275.0)
        self.assertEqual(sample['players'][1]['effective_health'], 301.0)
        self.assertTrue(sample['players'][3]['ready_to_revive'])
        self.assertEqual(sample['players'][3]['revival_time_percent'], 42)

    def test_status_owner_and_ranges_are_rejected(self):
        reader = Reader()
        reader.s32(0xB0000 + 0x440C, 2)
        with self.assertRaisesRegex(ValueError, 'ownership'):
            self.capture(reader)
        reader = Reader()
        reader.memory[0xE0000 + 0x4410] = bytes([2])
        with self.assertRaisesRegex(ValueError, 'revival'):
            self.capture(reader)
        reader = Reader()
        reader.s32(0xA0038, -1)
        with self.assertRaisesRegex(ValueError, 'kill count'):
            self.capture(reader)

    def test_signature_and_tracker_changes_are_rejected(self):
        reader = Reader()
        reader.memory[0x848977] = bytes(len(SIGNATURES[0x848977]))
        with self.assertRaisesRegex(ValueError, 'signatures'):
            self.capture(reader)
        reader = Reader()
        original = reader.u32
        calls = {'count': 0}

        def changing(address):
            if address == 0xDDE9A8:
                calls['count'] += 1
                if calls['count'] > 1:
                    return 0xDEADBEEF
            return original(address)

        reader.u32 = changing
        with self.assertRaisesRegex(ValueError, 'tracker changed'):
            self.capture(reader)


if __name__ == '__main__':
    unittest.main()
