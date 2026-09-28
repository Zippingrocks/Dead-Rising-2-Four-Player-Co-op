import struct
import unittest

from snapshot_connection_mesh import ProcessReader


class MemoryFixture(ProcessReader):
    def __init__(self):
        self.blocks = {}

    def read(self, address, length):
        for start, data in self.blocks.items():
            offset = address - start
            if 0 <= offset and offset + length <= len(data):
                return data[offset:offset + length]
        raise OSError(f"Unexpected memory read at {address:08X}")


class ListenerTests(unittest.TestCase):
    def setUp(self):
        self.reader = MemoryFixture()
        self.listener = bytearray(0x78)
        struct.pack_into('<I', self.listener, 0, 0x00CBA244)
        self.reader.blocks[0x1000] = self.listener

    def test_idle_listener_has_no_connection(self):
        result = self.reader.listener(0x1000)
        self.assertEqual(result['state'], 0)
        self.assertEqual(result['endpoints'], [])
        self.assertIsNone(result['connected'])

    def test_stock_reliable_has_only_three_pointer_slots(self):
        struct.pack_into('<I', self.listener, 0x68, 0x2000)
        connection = bytearray(0x104)
        struct.pack_into('<I', connection, 0x100, 0x3000)
        reliable = bytearray(0x90)
        struct.pack_into('<I', reliable, 0x8C, 0xC7F12000)  # Bandwidth, not endpoint 4.
        self.reader.blocks.update({0x2000: connection, 0x3000: reliable})
        self.assertEqual(self.reader.listener(0x1000)['endpoints'], [None, None, None])

    def test_wrong_build_fails_closed(self):
        struct.pack_into('<I', self.listener, 0, 0x00CBA208)
        with self.assertRaisesRegex(ValueError, 'Unsupported listener'):
            self.reader.listener(0x1000)

    def test_endpoint_identity_and_transport_address_are_distinct(self):
        endpoint = bytearray(0xA0)
        struct.pack_into('<QQH', endpoint, 0x78, 0x123456789, 0x0110000170000002, 5679)
        self.reader.blocks[0x4000] = endpoint
        result = self.reader.endpoint(0x4000)
        self.assertEqual(result['peer'], '0000000123456789')
        self.assertEqual(result['remote_address'], '0110000170000002')
        self.assertEqual(result['remote_port'], 5679)
        self.assertIsNone(result['admission_query_vtable'])

    def test_endpoint_exposes_native_admission_query_and_signaling_port(self):
        endpoint = bytearray(0xA0)
        struct.pack_into('<I', endpoint, 0x74, 0x5000)
        struct.pack_into('<H', endpoint, 0x8A, 5679)
        self.reader.blocks[0x4000] = endpoint
        self.reader.blocks[0x5000] = struct.pack('<I', 0x00CB83BC)
        result = self.reader.endpoint(0x4000)
        self.assertEqual(result['admission_query'], '0x00005000')
        self.assertEqual(result['admission_query_vtable'], '0x00CB83BC')
        self.assertEqual(result['signaling_port'], 5679)


class ClothingTests(unittest.TestCase):
    def setUp(self):
        self.reader = MemoryFixture()
        self.clothing = bytearray(0x3E14)
        self.heaps = bytearray(512)
        struct.pack_into('<I', self.clothing, 0, 0x00C48D30)
        self.reader.blocks.update({0x1000: self.clothing, 0xE11900: self.heaps, 0xE11640: bytearray(512)})
        for player in range(4):
            for slot in range(13):
                struct.pack_into('<i', self.clothing, 0x38 + (player * 13 + slot) * 0x10C, -1)

    def test_missing_heaps_are_not_provisioned(self):
        struct.pack_into('<i', self.clothing, 0x3DC4, 2)
        result = self.reader.clothing(0x1000)
        self.assertEqual(result['reserved_players'], 2)
        self.assertEqual(result['slots'][2]['heap_ids'], [-1] * 13)
        self.assertFalse(result['slots'][2]['all_heaps_live'])
        self.assertIsNone(self.reader.clothing(0))

    def test_ids_must_reference_live_heaps(self):
        for slot in range(13):
            struct.pack_into('<i', self.clothing, 0x38 + (2 * 13 + slot) * 0x10C, slot + 30)
            struct.pack_into('<I', self.heaps, (slot + 30) * 4, 0x200000 + slot * 4096)
        self.assertTrue(self.reader.clothing(0x1000)['slots'][2]['all_heaps_live'])
        struct.pack_into('<I', self.heaps, 30 * 4, 0)
        self.assertFalse(self.reader.clothing(0x1000)['slots'][2]['all_heaps_live'])

    def test_wrong_build_rejected(self):
        struct.pack_into('<I', self.clothing, 0, 0xBADBAD)
        with self.assertRaisesRegex(ValueError, 'Unsupported clothing'):
            self.reader.clothing(0x1000)


class GameActiveTests(unittest.TestCase):
    def setUp(self):
        self.reader = MemoryFixture()
        self.client = bytearray(0x100)
        self.reader.blocks.update({0x1000: self.client,
                                   0x88988C: bytes.fromhex('c6858d00000001'),
                                   0x7A1D29: bytes.fromhex('80be8d00000000')})

    def test_active_flag_is_not_a_byte_of_previous_stage(self):
        for previous_stage_byte, active in ((0, 1), (1, 0)):
            self.client[0x85] = previous_stage_byte
            self.client[0x8D] = active
            self.assertEqual(self.reader.game_active(0x1000), active)

    def test_both_native_instruction_signatures_are_required(self):
        for site in (0x88988C, 0x7A1D29):
            original = self.reader.blocks[site]
            self.reader.blocks[site] = bytes(7)
            with self.assertRaisesRegex(ValueError, 'Unsupported game-active'):
                self.reader.game_active(0x1000)
            self.reader.blocks[site] = original


class SyncStateTests(unittest.TestCase):
    def setUp(self):
        self.reader = MemoryFixture()

    def test_sync_cache_is_separate_from_flow_cache(self):
        data = bytearray(19 * 12)
        struct.pack_into('<B3xII', data, 12 * 12, 1, 0x7BA480, 0x2000)
        self.reader.blocks.update({0x87CDE5: bytes.fromhex('8990d4020000'), 0x12D0: data})
        cache = self.reader.sync_callbacks(0x1000)
        self.assertEqual(len(cache), 19)
        self.assertEqual(cache[12], {'point': 12, 'pending': 1, 'callback': '0x007BA480', 'user': '0x00002000'})
        self.assertEqual(sum(entry['pending'] for entry in cache), 1)

    def test_server_sync_keeps_each_confirmed_peer_distinct(self):
        server, local, records = bytearray(0x60), bytearray(0x4C), bytearray(4 * 0x48)
        struct.pack_into('<I', server, 0, 0xCBCEF8)
        struct.pack_into('<II', server, 0x54, 0x3000, 0x2000)
        struct.pack_into('<I', local, 0x48, 4)
        for i in range(4):
            struct.pack_into('<QIB', records, i * 0x48, 0x110000170000000 + i, i, 1)
            records[i * 0x48 + 0x1C + 12] = i % 2
        self.reader.blocks.update({0x1000: server, 0x2000: local, 0x3000: records})
        rows = self.reader.server_sync(0x1000)
        self.assertEqual([row['sync_received'][12] for row in rows], [0, 1, 0, 1])
        self.assertEqual([row['slot'] for row in rows], [0, 1, 2, 3])
        self.assertTrue(all(row['confirmed'] == 1 and len(row['flow_received']) == 10 for row in rows))
        struct.pack_into('<I', local, 0x48, 5)
        with self.assertRaisesRegex(ValueError, 'capacity'):
            self.reader.server_sync(0x1000)


class NetworkFileTests(unittest.TestCase):
    def setUp(self):
        self.reader = MemoryFixture()
        client, operation = bytearray(0xA4), bytearray(24)
        self.system, self.requests = bytearray(0x64), bytearray(2 * 0x28)
        struct.pack_into('<I', client, 0xA0, 0x2000)
        struct.pack_into('<6I', operation, 0, 0xCBCC04, 1, 0x1000, 0x3000, 0, 0xCAED3B91)
        struct.pack_into('<I', self.system, 0, 0xCB8468)
        struct.pack_into('<I', self.system, 0x20, 2)
        struct.pack_into('<I', self.system, 0x38, 0x4000)
        struct.pack_into('<10I', self.requests, 0, 0xCB8448, 0xCAED3B91, 0x5000, 15971,
                         0x2000, 0, 0, 2, 0, 0x55A)
        self.reader.blocks.update({0x1000: client, 0x2000: operation, 0x3000: self.system, 0x4000: self.requests,
                                   0x87BD3C: bytes.fromhex('8b4b20'), 0x87BD46: bytes.fromhex('8b5338'),
                                   0x87BD58: bytes.fromhex('83c028'), 0x863FB3: bytes.fromhex('8b4624c1e802')})

    def test_packed_state_and_inactive_slots_are_distinguished(self):
        result = self.reader.network_files(0x1000)
        self.assertEqual(result['request_capacity'], 2)
        self.assertEqual(len(result['active_requests']), 1)
        request = result['active_requests'][0]
        self.assertEqual((request['mode'], request['state'], request['link_id']), (2, 6, 2))
        self.assertEqual(request['size_or_receiver_address'], 15971)
        self.assertEqual(len(result['send_info']), 4)

    def test_transfer_arrays_use_allocation_counts_not_guessed_direction_order(self):
        struct.pack_into('<II', self.system, 0x24, 1, 2)
        struct.pack_into('<II', self.system, 0x3C, 0x5000, 0x6000)
        self.reader.blocks[0x5000] = struct.pack('<ifIifI', -1, 0, 0, 7, 1.5, 0x7000)
        self.reader.blocks[0x6000] = struct.pack('<ifI', 9, 2.5, 0x8000)
        arrays = self.reader.network_files(0x1000)['transfers']
        self.assertEqual([a['capacity'] for a in arrays], [2, 1])
        self.assertEqual([a['active'][0]['handle'] for a in arrays], [7, 9])

    def test_corrupt_capacity_and_vtables_fail_closed(self):
        struct.pack_into('<I', self.system, 0x20, 65)
        with self.assertRaisesRegex(ValueError, 'capacity'):
            self.reader.network_files(0x1000)
        struct.pack_into('<I', self.system, 0x20, 2)
        struct.pack_into('<I', self.requests, 0, 0xBAD)
        with self.assertRaisesRegex(ValueError, 'vtable'):
            self.reader.network_files(0x1000)


class LoadingStateTests(unittest.TestCase):
    def setUp(self):
        self.reader = MemoryFixture()
        self.reader.blocks.update({0x7BC02B: bytes.fromhex('8b4f38'),
                                   0x7BADA0: bytes.fromhex('8b86f8010000'),
                                   0x7BABE7: bytes.fromhex('8b86f4010000'),
                                   0xDDC3F0: struct.pack('<I', 0x1000)})
        self.game = bytearray(0x3C)
        self.manager = bytearray(0x1FC)
        struct.pack_into('<I', self.game, 0x38, 0x2000)
        self.reader.blocks.update({0x1000: self.game, 0x2000: self.manager})

    def test_native_loading_fields(self):
        struct.pack_into('<I', self.manager, 0, 2)
        struct.pack_into('<II', self.manager, 0x14, 3, 0x3000)
        struct.pack_into('<I', self.manager, 0x1F8, 5)
        struct.pack_into('<I', self.manager, 0x1F4, 3)
        state = self.reader.loading_state()
        self.assertEqual(state['sync_state'], 2)
        self.assertEqual(state['active_event'], 3)
        self.assertEqual(state['event_scene'], '0x00003000')
        self.assertEqual(state['finalizing_start_substate'], 5)
        self.assertEqual(state['start_level_substate'], 3)

    def test_absent_manager_is_not_ready_evidence(self):
        struct.pack_into('<I', self.game, 0x38, 0)
        self.assertIsNone(self.reader.loading_state())

    def test_wrong_build_rejected(self):
        self.reader.blocks[0x7BADA0] = bytes(6)
        with self.assertRaisesRegex(ValueError, 'Unsupported loading-state'):
            self.reader.loading_state()

    def test_flow_cache_retains_callback_and_user_ownership(self):
        data = bytearray(120)
        for command in range(10):
            struct.pack_into('<B3xII', data, command * 12, command % 2, 0x410000 + command * 16, 0x230000 + command * 4)
        self.reader.blocks[0x3258] = data
        cache = self.reader.flow_callbacks(0x3000)
        self.assertEqual(len(cache), 10)
        self.assertEqual(cache[3], {'command': 3, 'pending': 1, 'callback': '0x00410030', 'user': '0x0023000C'})
        self.assertEqual(cache[8]['pending'], 0)


if __name__ == '__main__':
    unittest.main()
