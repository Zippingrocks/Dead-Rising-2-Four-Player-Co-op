import struct
import unittest
from unittest.mock import patch

from snapshot_campaign_input import BUTTON_COUNT, SIGNATURES, capture_input


class Reader:
    def __init__(self):
        self.memory = dict(SIGNATURES)
        self.players = {'game': '0x1000', 'scene': '0x2000', 'manager': '0x3000', 'actors': [None, None, {
            'hidden': False, 'render_hidden': False, 'remote_enabled': 0, 'address': '0x4000'}, None]}
        for address, value in ((0x102C, 0x2000), (0x2098, 2), (0x2054, 0x8000), (0x4378, 0), (0x3014, 0x4000)):
            self.put(address, value)
        self.records = bytearray(BUTTON_COUNT * 28)
        self.records[4 * 28] = 1

    def put(self, address, value):
        self.memory[address] = struct.pack('<I', value)

    def read(self, address, size):
        if address == 0x8448 and size == len(self.records):
            return bytes(self.records)
        data = self.memory.get(address, bytes(size))
        if len(data) != size:
            raise AssertionError('Unexpected read size')
        return data

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]


class InputTests(unittest.TestCase):
    def capture(self, reader):
        with patch('snapshot_campaign_input.capture_players', return_value=reader.players):
            return capture_input(reader)

    def test_local_user_need_not_equal_input_device(self):
        result = self.capture(Reader())
        self.assertEqual(result['local_user'], 2)
        self.assertEqual(result['input_index'], 0)
        self.assertEqual(result['active_indices'], [4])
        self.assertEqual(len(result['buttons']), 95)

    def test_ownership_and_signature_rejected(self):
        for address, value in ((0x2098, 4), (0x2054, 0), (0x4378, 16), (0x3014, 0)):
            reader = Reader()
            reader.put(address, value)
            with self.subTest(address=address), self.assertRaises(ValueError):
                self.capture(reader)
        reader = Reader()
        reader.memory[0x44C780] = bytes(len(SIGNATURES[0x44C780]))
        with self.assertRaises(ValueError):
            self.capture(reader)

    def test_malformed_records_rejected(self):
        for offset in (0, 1, 2, 24):
            reader = Reader()
            reader.records[offset] = 2
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                self.capture(reader)
        reader = Reader()
        struct.pack_into('<f', reader.records, 4, float('nan'))
        with self.assertRaises(ValueError):
            self.capture(reader)

    def test_nonlocal_actor_rejected(self):
        reader = Reader()
        reader.players['actors'][2]['remote_enabled'] = 1
        with self.assertRaises(ValueError):
            self.capture(reader)
