import struct
import unittest
from unittest.mock import patch

from snapshot_campaign_camera import SIGNATURES, capture_camera


class Reader:
    def __init__(self):
        self.memory = dict(SIGNATURES)
        self.players = {'game': '0x1000', 'scene': '0x2000', 'actors': [None, {
            'hidden': False, 'render_hidden': False, 'remote_enabled': 0,
            'position': [4, 0, 6], 'address': '0x9000'}, None, None]}
        for address, value in ((0x102C, 0x2000), (0x2098, 1), (0x1030, 0x3000),
                               (0x3038, 0x4000), (0x4000, 0xCC3E40), (0x4008, 14),
                               (0x4018, 0xC3A2DC), (0x40D8, 0x5000),
                               (0x5000, 0xCD8CC4), (0x500C, 0x4000)):
            self.put(address, value)
        self.memory[0x401C] = struct.pack('<12f', 1, 2, 3, 1, 0, 0, 0, 1, 0, 43, 0.1, 1000)

    def put(self, address, value):
        self.memory[address] = struct.pack('<I', value)

    def read(self, address, size):
        data = self.memory.get(address, bytes(size))
        if len(data) != size:
            raise AssertionError('Unexpected read size')
        return data

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]


class CameraTests(unittest.TestCase):
    def capture(self, reader):
        with patch('snapshot_campaign_camera.capture_players', return_value=reader.players):
            return capture_camera(reader)

    def test_native_basis_and_local_owner(self):
        result = self.capture(Reader())
        self.assertEqual(result['local_user'], 1)
        self.assertEqual(result['ground_forward_xz'], [1, 0])
        self.assertEqual(result['ground_right_xz'], [0, 1])
        self.assertEqual(result['fov_degrees'], 43)

    def test_code_and_object_ownership_fail_closed(self):
        for address, value in ((0x4000, 0), (0x4008, 17), (0x4018, 0),
                               (0x500C, 0x4004), (0x2098, 4)):
            reader = Reader()
            reader.put(address, value)
            with self.subTest(address=address), self.assertRaises(ValueError):
                self.capture(reader)
        reader = Reader()
        reader.memory[0x7A0CA0] = bytes(len(SIGNATURES[0x7A0CA0]))
        with self.assertRaises(ValueError):
            self.capture(reader)

    def test_invalid_vectors_and_projection_fail_closed(self):
        for index, value in ((0, float('nan')), (3, 2), (7, 0), (9, 180), (10, -1), (11, 0)):
            reader = Reader()
            values = list(struct.unpack('<12f', reader.memory[0x401C]))
            values[index] = value
            reader.memory[0x401C] = struct.pack('<12f', *values)
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.capture(reader)

    def test_hidden_or_remote_local_actor_is_rejected(self):
        for key, value in (('hidden', True), ('render_hidden', True), ('remote_enabled', 1)):
            reader = Reader()
            reader.players['actors'][1][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.capture(reader)
