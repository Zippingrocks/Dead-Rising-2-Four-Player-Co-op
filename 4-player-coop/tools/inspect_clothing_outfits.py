import argparse
import ctypes
import struct


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400


def main():
    parser = argparse.ArgumentParser(description="Read DR2's live outfit table without modifying the process")
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--all", action="store_true", help="print every non-empty outfit")
    args = parser.parse_args()

    process = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, args.pid)
    if not process:
        raise ctypes.WinError(ctypes.get_last_error())

    def read(address, size):
        output = ctypes.create_string_buffer(size)
        count = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(process, ctypes.c_void_p(address), output, size,
                                          ctypes.byref(count)) or count.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return output.raw

    def u32(address):
        return struct.unpack("<I", read(address, 4))[0]

    def string(address):
        length = read(address + 0x20, 1)[0]
        data = u32(address) if length >= 0x1F else address
        output = bytearray()
        for offset in range(260):
            value = read(data + offset, 1)[0]
            if not value:
                break
            output.append(value)
        return output.decode("ascii", errors="replace")

    try:
        game = u32(0x00DCB0FC)
        manager = u32(game + 0x7EB8)
        database = u32(manager + 0x3CEC)
        print(f"pid={args.pid} game=0x{game:08X} manager=0x{manager:08X} database=0x{database:08X}")
        for outfit in range(191):
            entry = database + outfit * 0x11C + 0x88
            parts = [string(entry + part * 36 + 0x1C) for part in range(7)]
            if args.all or any("tir" in value.lower() for value in parts) or outfit in (7, 150, 151, 152):
                print(f"{outfit:3}: " + " | ".join(parts))
    finally:
        kernel32.CloseHandle(process)


if __name__ == "__main__":
    main()
