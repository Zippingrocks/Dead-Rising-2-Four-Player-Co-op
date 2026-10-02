import struct
import unittest
from unittest.mock import patch

from snapshot_campaign_props import SIGNATURES, capture_props


class Reader:
    def __init__(self):
        self.memory = dict(SIGNATURES)
        self.players = {'game': '0x1000', 'scene': '0x2000'}
        for address, value in ((0x102C, 0x2000), (0x2090, 0x3000), (0x3020, 0x4000),
                               (0x70AC, 0x8000), (0x8040, 168), (0x8024, 7)):
            self.memory[address] = struct.pack('<I', value)
        self.memory[0x4030] = struct.pack('<2048I', 0x7000, *([0] * 2047))
        self.memory[0x8000] = struct.pack('<3f', 4, -1.1, 24)
        self.memory[0x7094] = b'\x03'
        for offset, byte in enumerate(b'bat\0'):
            self.memory[0x7074 + offset] = bytes([byte])

    def read(self, address, size):
        data = self.memory.get(address, bytes(size))
        if len(data) != size:
            raise AssertionError('Unexpected read size')
        return data

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]


class PropTests(unittest.TestCase):
    def capture(self, reader, ids=None):
        with patch('snapshot_campaign_props.capture_players', return_value=reader.players):
            return capture_props(reader, ids)

    def test_identity_position_and_filter(self):
        result = self.capture(Reader(), {168})['props'][0]
        self.assertEqual((result['slot'], result['item_id'], result['name']), (0, 168, 'bat'))
        self.assertEqual(result['position'][0], 4)
        self.assertEqual(self.capture(Reader(), {73})['props'], [])

    def test_heap_name_and_bounded_termination(self):
        reader = Reader()
        reader.memory[0x7094] = b'\x1f'
        reader.memory[0x7074] = struct.pack('<I', 0x9000)
        for offset in range(256):
            reader.memory[0x9000 + offset] = b'x'
        with self.assertRaises(ValueError):
            self.capture(reader)
        reader.memory[0x9004] = b'\0'
        self.assertEqual(self.capture(reader)['props'][0]['name'], 'xxxx')

    def test_bad_signature_metadata_and_position(self):
        for address, data in ((0x70AC, bytes(4)), (0x3020, bytes(4)),
                              (0x8000, struct.pack('<3f', 0, float('inf'), 0)),
                              (0x4105E0, bytes(len(SIGNATURES[0x4105E0])))):
            reader = Reader()
            reader.memory[address] = data
            with self.subTest(address=address), self.assertRaises(ValueError):
                self.capture(reader)

    def test_scene_change_rejected(self):
        reader = Reader()
        reader.memory[0x102C] = bytes(4)
        with self.assertRaises(ValueError):
            self.capture(reader)
