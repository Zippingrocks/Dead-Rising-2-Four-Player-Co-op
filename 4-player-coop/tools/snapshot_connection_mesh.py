"""Read native DR2 topology and mesh state without injecting code or modifying memory."""

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
import struct


class ProcessReader:
    def __init__(self, pid):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.ReadProcessMemory.argtypes = [
            wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.kernel.ReadProcessMemory.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.QueryFullProcessImageNameW.argtypes = [
            wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD),
        ]
        self.handle = self.kernel.OpenProcess(0x1010, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        path = ctypes.create_unicode_buffer(32768)
        length = wintypes.DWORD(len(path))
        if not self.kernel.QueryFullProcessImageNameW(self.handle, 0, path, ctypes.byref(length)):
            self.close()
            raise ctypes.WinError(ctypes.get_last_error())
        self.path = path.value
        if self.path.rsplit("\\", 1)[-1].lower() != "deadrising2.exe":
            self.close()
            raise ValueError("The selected process is not deadrising2.exe")

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    def read(self, address, length):
        result = ctypes.create_string_buffer(length)
        actual = ctypes.c_size_t()
        if not self.kernel.ReadProcessMemory(
            self.handle, ctypes.c_void_p(address), result, length, ctypes.byref(actual)
        ) or actual.value != length:
            raise OSError(f"Cannot read {length} bytes at 0x{address:08X}")
        return result.raw

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def u8(self, address):
        return self.read(address, 1)[0]

    def endpoint(self, address):
        if not address:
            return None
        data = self.read(address, 0xA0)
        query = struct.unpack_from('<I', data, 0x74)[0]
        return {
            "address": f"0x{address:08X}",
            "admission_query": f"0x{query:08X}",
            "admission_query_vtable": f"0x{self.u32(query):08X}" if query else None,
            "peer": f"{struct.unpack_from('<Q', data, 0x78)[0]:016X}",
            "remote_address": f"{struct.unpack_from('<Q', data, 0x80)[0]:016X}",
            "remote_port": struct.unpack_from('<H', data, 0x88)[0],
            "signaling_port": struct.unpack_from('<H', data, 0x8A)[0],
            "state": struct.unpack_from("<i", data, 0x98)[0],
            "ready": data[0x9C],
        }

    def listener(self, address):
        if not address:
            return None
        data = self.read(address, 0x78)
        vtable = struct.unpack_from('<I', data)[0]
        if vtable != 0x00CBA244:
            raise ValueError(f"Unsupported listener vtable 0x{vtable:08X}; refusing guessed offsets")
        connection = struct.unpack_from("<I", data, 0x68)[0]
        reliable = self.u32(connection + 0x100) if connection else 0
        return {
            "address": f"0x{address:08X}",
            "vtable": f"0x{vtable:08X}",
            "state": struct.unpack_from("<i", data, 0x64)[0],
            "started": data[0x6C],
            "connected": self.endpoint(struct.unpack_from("<I", data, 0x70)[0]),
            "connection": f"0x{connection:08X}",
            "reliable": f"0x{reliable:08X}",
            "reliable_state": self.u32(reliable + 0x0C) if reliable else None,
            "endpoint_count": self.u32(reliable + 0x7C) if reliable else None,
            "endpoints": [self.endpoint(self.u32(reliable + 0x80 + i * 4)) for i in range(3)]
            if reliable else [],
        }

    def clothing(self, address):
        if not address:
            return None
        data = self.read(address, 0x3E14)
        vtable = struct.unpack_from('<I', data)[0]
        if vtable != 0x00C48D30:
            raise ValueError(f'Unsupported clothing vtable 0x{vtable:08X}')
        heap_table = struct.unpack('<128I', self.read(0xE11900, 512))
        alternate_heaps = struct.unpack('<128I', self.read(0xE11640, 512))
        slots = []
        for player in range(4):
            ids = [struct.unpack_from('<i', data, 0x38 + (player * 13 + slot) * 0x10C)[0]
                   for slot in range(13)]
            slots.append({
                'player': player,
                'actor': f"0x{struct.unpack_from('<I', data, 0x3CDC + 4 * player)[0]:08X}",
                'heap_ids': ids,
                'all_heaps_live': all(0 < value < 128 and (heap_table[value] or alternate_heaps[value])
                                      for value in ids),
            })
        return {'address': f'0x{address:08X}', 'vtable': f'0x{vtable:08X}',
                'mode': struct.unpack_from('<i', data, 8)[0],
                'reserved_players': struct.unpack_from('<i', data, 0x3DC4)[0],
                'pending_loads': struct.unpack_from('<i', data, 0x3DEC)[0],
                'completed_loads': struct.unpack_from('<i', data, 0x3CD8)[0], 'slots': slots}

    def loading_state(self):
        # Matched PC update/finalization code establishes these offsets independently of OTR's layout.
        signatures = {0x7BC02B: bytes.fromhex('8b4f38'), 0x7BADA0: bytes.fromhex('8b86f8010000'),
                      0x7BABE7: bytes.fromhex('8b86f4010000')}
        if any(self.read(site, len(expected)) != expected for site, expected in signatures.items()):
            raise ValueError('Unsupported loading-state code signatures')
        game = self.u32(0xDDC3F0)
        manager = self.u32(game + 0x38) if game else 0
        if not manager:
            return None
        data = self.read(manager, 0x1FC)
        return {'address': f'0x{manager:08X}', 'game': f'0x{game:08X}',
                'sync_state': struct.unpack_from('<I', data, 0)[0],
                'active_event': struct.unpack_from('<I', data, 0x14)[0],
                'event_scene': f"0x{struct.unpack_from('<I', data, 0x18)[0]:08X}",
                'start_level_substate': struct.unpack_from('<I', data, 0x1F4)[0],
                'finalizing_start_substate': struct.unpack_from('<I', data, 0x1F8)[0]}

    def game_active(self, client):
        # PC StartGame writes +8D; AsyncWaitForClientJoining reads the same byte.
        signatures = {0x88988C: bytes.fromhex('c6858d00000001'),
                      0x7A1D29: bytes.fromhex('80be8d00000000')}
        if any(self.read(site, len(expected)) != expected for site, expected in signatures.items()):
            raise ValueError('Unsupported game-active code signatures')
        return self.u8(client + 0x8D)

    def flow_callbacks(self, client):
        data = self.read(client + 0x258, 120)
        return [{'command': command, 'pending': data[command * 12],
                 'callback': f"0x{struct.unpack_from('<I', data, command * 12 + 4)[0]:08X}",
                 'user': f"0x{struct.unpack_from('<I', data, command * 12 + 8)[0]:08X}"}
                for command in range(10)]

    def sync_callbacks(self, client):
        if self.read(0x87CDE5, 6) != bytes.fromhex('8990d4020000'):
            raise ValueError('Unsupported sync callback layout')
        data = self.read(client + 0x2D0, 19 * 12)
        return [{'point': point, 'pending': data[point * 12],
                 'callback': f"0x{struct.unpack_from('<I', data, point * 12 + 4)[0]:08X}",
                 'user': f"0x{struct.unpack_from('<I', data, point * 12 + 8)[0]:08X}"}
                for point in range(19)]

    def server_sync(self, server):
        if not server:
            return None
        if self.u32(server) != 0x00CBCEF8:
            raise ValueError('Unsupported native server sync layout')
        local = self.u32(server + 0x58)
        records = self.u32(server + 0x54)
        count = self.u32(local + 0x48) if local else 0
        if count > 4 or (count and not records):
            raise ValueError('Invalid native server sync capacity')
        data = self.read(records, count * 0x48) if count else b''
        return [{'peer': f"{struct.unpack_from('<Q', data, index * 0x48)[0]:016X}",
                 'slot': struct.unpack_from('<I', data, index * 0x48 + 8)[0],
                 'confirmed': data[index * 0x48 + 0xC], 'leaving': data[index * 0x48 + 0xD],
                 'flow_received': list(data[index * 0x48 + 0x12:index * 0x48 + 0x1C]),
                 'sync_received': list(data[index * 0x48 + 0x1C:index * 0x48 + 0x2F])}
                for index in range(count)]

    def network_files(self, client):
        # PC Request/Init/SetState instructions establish table stride and packed state bits.
        signatures = {0x87BD3C: bytes.fromhex('8b4b20'), 0x87BD46: bytes.fromhex('8b5338'),
                      0x87BD58: bytes.fromhex('83c028'), 0x863FB3: bytes.fromhex('8b4624c1e802')}
        if any(self.read(site, len(expected)) != expected for site, expected in signatures.items()):
            raise ValueError('Unsupported network-file request layout')
        operation = self.u32(client + 0xA0)
        if not operation:
            return None
        if self.u32(operation) != 0xCBCC04 or self.u32(operation + 8) != client:
            raise ValueError('Unsupported client network-file operation')
        system = self.u32(operation + 12)
        if not system or self.u32(system) != 0xCB8468:
            raise ValueError('Unsupported network-file system')
        data = self.read(system, 0x64)
        count = struct.unpack_from('<I', data, 0x20)[0]
        table = struct.unpack_from('<I', data, 0x38)[0]
        if count > 64 or (count and not table):
            raise ValueError('Invalid network-file request capacity')
        requests = []
        for index in range(count):
            words = struct.unpack('<10I', self.read(table + index * 0x28, 0x28))
            flags = words[9]
            if not flags & 3:
                continue
            if words[0] != 0xCB8448:
                raise ValueError('Unsupported network-file request vtable')
            requests.append({'index': index, 'address': f'0x{table + index * 0x28:08X}',
                             'mode': flags & 3, 'state': (flags >> 2) & 7,
                             'hash_or_filename': f'0x{words[1]:08X}',
                             'buffer_or_receiver_address': f'0x{words[2]:08X}',
                             'size_or_receiver_address': words[3], 'user': f'0x{words[4]:08X}',
                             'handle': words[5], 'transfer': f'0x{words[6]:08X}',
                             'link_id': words[7], 'packed_flags': f'0x{flags:08X}'})
        # Init allocates +3C from the +28 count and +40 from +24. Keep field names literal.
        transfers = []
        for pointer_offset, count_offset in ((0x3C, 0x28), (0x40, 0x24)):
            pointer = struct.unpack_from('<I', data, pointer_offset)[0]
            capacity = struct.unpack_from('<I', data, count_offset)[0]
            if capacity > 64 or (capacity and not pointer):
                raise ValueError('Invalid network-file transfer capacity')
            slots = []
            for index in range(capacity):
                handle, elapsed, transfer = struct.unpack('<ifI', self.read(pointer + index * 12, 12))
                if handle != -1:
                    slots.append({'index': index, 'handle': handle, 'receive_age_seconds': elapsed,
                                  'object': f'0x{transfer:08X}'})
            transfers.append({'pointer_offset': f'0x{pointer_offset:X}', 'capacity': capacity, 'active': slots})
        return {'address': f'0x{system:08X}', 'state': struct.unpack_from('<I', data, 12)[0],
                'operation': f'0x{operation:08X}', 'operation_handle': self.u32(operation + 16),
                'operation_hash': f'0x{self.u32(operation + 20):08X}', 'request_capacity': count,
                'active_requests': requests, 'transfers': transfers,
                'send_info': [{'slot': i, 'index': struct.unpack_from('<i', data, 0x44 + 8 * i)[0],
                               'count': struct.unpack_from('<i', data, 0x48 + 8 * i)[0]} for i in range(4)]}

    def snapshot(self):
        if self.read(0x400000, 2) != b"MZ":
            raise ValueError("Expected retail DR2 image base was not found")
        online = self.u32(0xE5F428)
        p2p = self.u32(online + 0xD8) if online else 0
        client = self.u32(p2p + 0x38) if p2p else 0
        manager = self.u32(p2p + 0x18) if p2p else 0
        listener = self.u32(manager + 0xBC) if manager else 0
        mesh = self.u32(0xDDEA04)
        mesh_manager = self.u32(mesh + 0x34) if mesh else 0
        mesh_listener = self.u32(mesh_manager + 0xBC) if mesh_manager else 0
        clothing, clothing_error = None, None
        try:
            game_context = self.u32(0xDCB0FC)
            clothing = self.clothing(self.u32(game_context + 0x7EB8)) if game_context else None
        except (OSError, ValueError) as error:
            clothing_error = str(error)
        loading, loading_error = None, None
        try:
            loading = self.loading_state()
        except (OSError, ValueError) as error:
            loading_error = str(error)
        server_sync, server_sync_error = None, None
        try:
            server_sync = self.server_sync(self.u32(p2p + 0x34)) if p2p else None
        except (OSError, ValueError) as error:
            server_sync_error = str(error)
        network_files, network_files_error = None, None
        try:
            network_files = self.network_files(client) if client else None
        except (OSError, ValueError) as error:
            network_files_error = str(error)
        return {
            "image": self.path,
            "clothing": clothing,
            "clothing_error": clothing_error,
            "loading": loading,
            "loading_error": loading_error,
            "server_sync": server_sync,
            "server_sync_error": server_sync_error,
            "network_files": network_files,
            "network_files_error": network_files_error,
            "online": f"0x{online:08X}",
            "p2p": f"0x{p2p:08X}",
            "client": {
                "address": f"0x{client:08X}",
                "stage": self.u32(client + 0x88),
                "local_node": f"0x{self.u32(client + 0x90):08X}",
                "single_player": self.u8(client + 0x9D),
                "game_active": self.game_active(client),
                "game_active_offset": "0x8D",
                "transfer_active": self.u8(client + 0x97),
                "game_started": self.u8(client + 0x251),
                "jip_state": self.u32(client + 0x3F4),
                "jip_bytes": self.u32(client + 0x3F8),
                "jip_buffer": f"0x{self.u32(client + 0x3FC):08X}",
                "flow_callbacks": self.flow_callbacks(client),
                "sync_callbacks": self.sync_callbacks(client),
            } if client else None,
            "topology_manager": f"0x{manager:08X}",
            "listener": self.listener(listener),
            "mesh_topology_manager": f"0x{mesh_manager:08X}",
            "mesh_listener": self.listener(mesh_listener),
            "mesh": {
                "address": f"0x{mesh:08X}",
                "state": self.u32(mesh + 0x3C),
                "players": self.u32(mesh + 0x40),
                "local_id": self.u32(mesh + 0x44),
                "topology": f"0x{self.u32(mesh + 0x88):08X}",
                "member": f"0x{self.u32(mesh + 0x90):08X}",
                "connect_next": self.u8(mesh + 0x94),
                "hello": self.u32(mesh + 0x98),
                "host_done": self.u8(mesh + 0x9C),
                "client_done": self.u32(mesh + 0xA0),
                "host_start": self.u8(mesh + 0xA4),
            } if mesh else None,
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    args = parser.parse_args()
    reader = ProcessReader(args.pid)
    try:
        result = reader.snapshot()
        result.update(pid=args.pid, captured_at=datetime.now(timezone.utc).isoformat())
        print(json.dumps(result, indent=2))
    finally:
        reader.close()


if __name__ == "__main__":
    main()
