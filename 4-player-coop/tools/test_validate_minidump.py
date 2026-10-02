import struct
import tempfile
from pathlib import Path
import unittest

from validate_minidump import validate_dump


def fixture():
    data = bytearray(288)
    data[:4] = b'MDMP'
    struct.pack_into('<II', data, 8, 3, 32)
    for index, row in enumerate(((3, 52, 68), (4, 112, 120), (9, 32, 232))):
        struct.pack_into('<III', data, 32 + index * 12, *row)
    struct.pack_into('<I', data, 68, 1)
    struct.pack_into('<I', data, 120, 1)
    struct.pack_into('<QQQQ', data, 232, 1, 264, 0x400000, 24)
    return data


class DumpValidationTests(unittest.TestCase):
    def check(self, data):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as folder:
            path = Path(folder) / 'fixture.dmp'
            path.write_bytes(data)
            return validate_dump(path)

    def test_complete(self):
        result = self.check(fixture())
        self.assertTrue(result['valid'])
        self.assertEqual(result['memory_bytes'], 24)

    def test_unfinalized_header(self):
        data = fixture()
        struct.pack_into('<II', data, 8, 0, 0)
        self.assertFalse(self.check(data)['valid'])

    def test_truncated_payload(self):
        self.assertFalse(self.check(fixture()[:-1])['valid'])

    def test_invalid_or_empty_streams(self):
        for offset, value in ((8, 257), (68, 0), (120, 2), (232, 999999), (56, 3)):
            with self.subTest(offset=offset):
                data = fixture()
                struct.pack_into('<I', data, offset, value)
                self.assertFalse(self.check(data)['valid'])

    def test_short_or_wrong_file(self):
        self.assertFalse(self.check(b'')['valid'])
        data = fixture()
        data[:4] = b'nope'
        self.assertFalse(self.check(data)['valid'])


if __name__ == '__main__':
    unittest.main()
