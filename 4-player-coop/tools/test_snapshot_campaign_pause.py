import struct
import unittest

from snapshot_campaign_pause import SIGNATURES, capture_pause


class Reader:
    def __init__(self, count=4):
        self.memory = {0x400000: b'MZ', **SIGNATURES}
        for address, value in ((0xDDC3F0, 0x1000), (0xE5F428, 0x2000),
                               (0x1038, 0x3000), (0x20D8, 0x4000),
                               (0x4038, 0x5000), (0x5088, 2),
                               (0x5028, 0x6000), (0x607C, 0x7000)):
            self.memory[address] = struct.pack('<I', value)
        self.memory[0x31DC] = struct.pack('<4I', 8, 8, 8, 0)
        for slot, offset in enumerate((0x54, 0x6C, 0x84, 0x9C)):
            entry = 0x8000 + slot * 0x100
            self.memory[0x7000 + offset] = struct.pack('<I', entry)
            self.memory[entry + 8] = struct.pack('<Q', slot + 1 if slot < count else 0)

    def read(self, address, length):
        data = self.memory[address]
        if len(data) != length:
            raise AssertionError('Unexpected read size')
        return data

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]


class PauseTests(unittest.TestCase):
    def test_two_and_four_native_counts(self):
        for count in (2, 4):
            sample = capture_pause(Reader(count))
            self.assertEqual(sample['native_count'], count)
            self.assertEqual(sample['pause_masks'], [8, 8, 8, 0])

    def test_stage_four_uses_native_solo_count(self):
        reader = Reader()
        reader.memory[0x5088] = struct.pack('<I', 4)
        self.assertEqual(capture_pause(reader)['native_count'], 1)

    def test_reject_signature_and_identity_alias(self):
        reader = Reader()
        reader.memory[0x853B00] = b'\0' * len(SIGNATURES[0x853B00])
        with self.assertRaisesRegex(ValueError, 'signatures'):
            capture_pause(reader)
        reader = Reader()
        reader.memory[0x8108] = reader.memory[0x8008]
        with self.assertRaisesRegex(ValueError, 'Aliased'):
            capture_pause(reader)

    def test_reject_owner_change(self):
        reader = Reader()
        original = reader.read
        def read(address, length):
            if address == 0x31DC:
                reader.memory[0x5088] = struct.pack('<I', 4)
            return original(address, length)
        reader.read = read
        with self.assertRaisesRegex(ValueError, 'changed'):
            capture_pause(reader)


if __name__ == '__main__':
    unittest.main()
