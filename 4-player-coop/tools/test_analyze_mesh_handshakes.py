import json
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import unittest

from analyze_mesh_handshakes import analyze, decode_syn, LOCAL_ID_BASE


def header(peer=LOCAL_ID_BASE + 1, flags=1, sequence=0x1234, ack=0):
    return struct.pack('<H', 0) + struct.pack('>BBHHHHHQ', flags, 0, sequence, ack, 0, 65535, 0, peer)


class MeshHandshakeTests(unittest.TestCase):
    def test_syn_wire_endianness(self):
        result = decode_syn(header())
        self.assertEqual(result['sequence'], 0x1234)
        self.assertEqual(result['peer_id'], '0110000170000001')
        self.assertEqual(result['unreliable_offset'], 65535)

    def test_ack_syn(self):
        result = decode_syn(header(flags=3, ack=0x5678))
        self.assertEqual(result['flags'], 3)
        self.assertEqual(result['cumulative_ack'], 0x5678)

    def test_truncated_and_non_syn_rejected(self):
        for data in (header()[:-1], header() + b'\0', header(flags=2)):
            with self.assertRaises(ValueError):
                decode_syn(data)

    def test_observed_native_host_accepted_header(self):
        # Run 164738: exact sender/receiver match followed by native CanListen accepted=1.
        result = decode_syn(bytes.fromhex('1400010003E800000000000000000110000170000001'))
        self.assertEqual(result['transport_prefix_le16'], 20)
        self.assertEqual(result['sequence'], 1000)
        self.assertEqual(result['peer_id'], '0110000170000001')

    def test_transport_correlation_and_admission_remain_separate(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            raw = header().hex()
            (run / 'coop_net.1.log').write_text(
                f'mesh handshake: direction=send peer=0110000170000002 channel=5679 bytes=22 header={raw}\n', encoding='utf-8')
            (run / 'coop_net.2.log').write_text(
                f'mesh handshake: direction=receive peer=0110000170000001 channel=5679 bytes=22 header={raw}\n'
                'mesh admission: query=12345678 nonce=0110000170000001 remoteAddress=0110000170000001 port=5679 accepted=0\n', encoding='utf-8')
            result = analyze(run)
            self.assertTrue(result['packets'][0]['identical_receive_captured'])
            self.assertFalse(result['admissions'][0]['accepted'])
            self.assertEqual(result['identity_mismatches'], [])
            self.assertEqual(result['decode_errors'], [])
            json.dumps(result)

    def test_identity_mismatch_and_bad_size(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            raw = header(peer=LOCAL_ID_BASE + 3).hex()
            (run / 'coop_net.1.log').write_text(
                f'mesh handshake: direction=send peer=0110000170000002 channel=5679 bytes=22 header={raw}\n'
                f'mesh handshake: direction=send peer=0110000170000002 channel=5679 bytes=21 header={raw}\n', encoding='utf-8')
            result = analyze(run)
            self.assertEqual(len(result['identity_mismatches']), 1)
            self.assertEqual(len(result['decode_errors']), 1)
            self.assertFalse(result['packets'][0]['identical_receive_captured'])

    def test_no_capture_is_not_success(self):
        with TemporaryDirectory() as directory:
            result = analyze(Path(directory))
            self.assertEqual(result['packets'], [])
            self.assertNotIn('connected', result)


if __name__ == '__main__':
    unittest.main()
