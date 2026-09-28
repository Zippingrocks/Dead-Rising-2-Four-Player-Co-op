import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32


def main():
    args = sys.argv[1:]
    mapped = args[0] == "--mapped"
    if mapped:
        args = args[1:]
    path = args[0]
    start = int(args[1], 0)
    end = int(args[2], 0)
    image = open(path, "rb").read()
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    section_count = struct.unpack_from("<H", image, pe + 6)[0]
    optional_size = struct.unpack_from("<H", image, pe + 20)[0]
    optional = pe + 24
    image_base = struct.unpack_from("<I", image, optional + 28)[0]
    sections = optional + optional_size
    rva = start - image_base
    if mapped:
        if not 0 <= rva < rva + end - start <= len(image):
            raise SystemExit("range is outside the mapped image")
        for instruction in Cs(CS_ARCH_X86, CS_MODE_32).disasm(image[rva:rva + end - start], start):
            print(f"{instruction.address:08X}  {instruction.mnemonic:<8} {instruction.op_str}")
        return
    for index in range(section_count):
        entry = sections + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", image, entry + 8)
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            offset = raw_offset + rva - virtual_address
            code = image[offset:offset + end - start]
            disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
            for instruction in disassembler.disasm(code, start):
                if instruction.address >= end:
                    break
                print(f"{instruction.address:08X}  {instruction.mnemonic:<8} {instruction.op_str}")
            return
    raise SystemExit(f"virtual address 0x{start:X} is not in a PE section")


if __name__ == "__main__":
    main()
