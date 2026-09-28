import ctypes
import struct
import sys


PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT = 0x1000
PAGE_GUARD = 0x100
PAGE_NOACCESS = 0x01


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.c_ulong),
        ("RegionSize", ctypes.c_size_t),
        ("State", ctypes.c_ulong),
        ("Protect", ctypes.c_ulong),
        ("Type", ctypes.c_ulong),
    ]


def main():
    if len(sys.argv) not in (3, 5):
        raise SystemExit("usage: xref_process_call.py PID TARGET [START END]")

    pid = int(sys.argv[1], 0)
    target = int(sys.argv[2], 0)
    start = int(sys.argv[3], 0) if len(sys.argv) == 5 else 0x00400000
    end = int(sys.argv[4], 0) if len(sys.argv) == 5 else 0x01000000

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())

    try:
        cursor = start
        mbi = MEMORY_BASIC_INFORMATION()
        while cursor < end:
            queried = kernel32.VirtualQueryEx(
                handle, ctypes.c_void_p(cursor), ctypes.byref(mbi), ctypes.sizeof(mbi)
            )
            if not queried:
                break

            base = int(mbi.BaseAddress or 0)
            region_end = min(base + int(mbi.RegionSize), end)
            readable = (
                mbi.State == MEM_COMMIT
                and not (mbi.Protect & PAGE_GUARD)
                and (mbi.Protect & 0xFF) != PAGE_NOACCESS
            )
            if readable and region_end > max(base, start):
                read_start = max(base, start)
                size = region_end - read_start
                buffer = ctypes.create_string_buffer(size)
                bytes_read = ctypes.c_size_t()
                if kernel32.ReadProcessMemory(
                    handle,
                    ctypes.c_void_p(read_start),
                    buffer,
                    size,
                    ctypes.byref(bytes_read),
                ):
                    data = buffer.raw[: bytes_read.value]
                    for offset in range(0, max(0, len(data) - 4)):
                        if data[offset] != 0xE8:
                            continue
                        displacement = struct.unpack_from("<i", data, offset + 1)[0]
                        callsite = read_start + offset
                        if callsite + 5 + displacement == target:
                            print(f"{callsite:08X} -> {target:08X}")

            next_cursor = base + int(mbi.RegionSize)
            if next_cursor <= cursor:
                break
            cursor = next_cursor
    finally:
        kernel32.CloseHandle(handle)


if __name__ == "__main__":
    main()
