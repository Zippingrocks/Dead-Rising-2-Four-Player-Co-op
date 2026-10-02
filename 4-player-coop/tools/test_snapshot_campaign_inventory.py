import struct
import unittest
from unittest.mock import patch

from snapshot_campaign_inventory import SIGNATURES, capture_inventory


class Reader:
    def __init__(self):
        self.memory = dict(SIGNATURES)
        self.players = {'game': '0x1000', 'scene': '0x2000', 'actors': [
            {'address': f'0x{0x8000 + slot * 0x20000:X}', 'remote_enabled': int(slot != 2),
             'hidden': False, 'render_hidden': False} for slot in range(4)]}
        for address, value in ((0x102C, 0x2000), (0x2098, 2), (0x2090, 0x3000),
                               (0x3030, 0x4000), (0x48000 + 0xD82C, 0x90000),
                               (0x90000, 0xC877A8), (0x94408, 0x48000),
                               (0xC877A8 + 0x5C, 0x6BCF10), (0xC9A720 + 0xBC, 0x7581A0),
                               (0xDDE9A8, 0xA0000), (0xA0008, 0xB0000)):
            self.put(address, value)
        self.memory[0xB002C] = struct.pack('<f', 300)
        self.memory[0x9447C] = struct.pack('<f', 400)
        for slot in range(4):
            record = 0x4010 + slot * 0x78
            self.put(record, 0x8000 + slot * 0x20000)
            self.memory[record + 0x68] = struct.pack('<i', -1)
        self.put(0x4010 + 2 * 0x78 + 4, 0xC0000)
        self.put(0x4010 + 2 * 0x78 + 0x68, 0)
        self.put(0xC00AC, 0xD0000)
        self.put(0xD0040, 168)

    def put(self, address, value):
        self.memory[address] = struct.pack('<I', value)

    def read(self, address, size):
        data = self.memory.get(address, bytes(size))
        if len(data) != size:
            raise AssertionError('Unexpected read size')
        return data

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]


class InventoryTests(unittest.TestCase):
    def capture(self, reader):
        with patch('snapshot_campaign_inventory.capture_players', return_value=reader.players):
            return capture_inventory(reader)

    def test_owner_item_and_local_health(self):
        sample = self.capture(Reader())
        self.assertEqual(sample['local_user'], 2)
        self.assertEqual(sample['local_health'], 300)
        self.assertEqual(sample['local_maximum_health'], 400)
        self.assertEqual(sample['inventory'][2]['selected_item'], 168)
        self.assertTrue(all(item is None for item in sample['inventory'][0]['items']))

    def test_hidden_or_wrong_owner_rejected(self):
        reader = Reader()
        reader.players['actors'][2]['hidden'] = True
        with self.assertRaisesRegex(ValueError, 'hidden'):
            self.capture(reader)
        reader = Reader()
        reader.put(0x4010, 0x48000)
        with self.assertRaisesRegex(ValueError, 'owner differs'):
            self.capture(reader)

    def test_bad_selection_and_health_rejected(self):
        reader = Reader()
        reader.put(0x4010 + 0x68, 12)
        with self.assertRaisesRegex(ValueError, 'selected'):
            self.capture(reader)
        reader = Reader()
        reader.memory[0xB002C] = struct.pack('<f', float('nan'))
        with self.assertRaisesRegex(ValueError, 'Non-finite'):
            self.capture(reader)

    def test_unsupported_signature(self):
        reader = Reader()
        reader.memory[0x758190] = bytes(7)
        with self.assertRaisesRegex(ValueError, 'signatures'):
            self.capture(reader)


if __name__ == '__main__':
    unittest.main()
