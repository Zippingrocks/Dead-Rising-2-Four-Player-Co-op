"""Find immediate-three comparisons near cReliableLayer endpoint-array accesses."""

import argparse
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def executable_sections(image, mapped):
    pe = u32(image, 0x3C)
    section_count = u16(image, pe + 6)
    optional_size = u16(image, pe + 20)
    optional = pe + 24
    image_base = u32(image, optional + 28)
    table = optional + optional_size
    for index in range(section_count):
        entry = table + index * 40
        name = image[entry:entry + 8].rstrip(b"\0").decode("ascii", "replace")
        virtual_size, virtual_address, raw_size, raw_address = struct.unpack_from(
            "<IIII", image, entry + 8
        )
        characteristics = u32(image, entry + 36)
        if characteristics & 0x20000000 and raw_size:
            offset = virtual_address if mapped else raw_address
            size = virtual_size if mapped else raw_size
            yield name, image_base + virtual_address, image[offset:offset + size]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("--mapped", action="store_true")
    parser.add_argument("--min-address", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--max-address", type=lambda value: int(value, 0), default=0xFFFFFFFF)
    parser.add_argument("--tail-accesses", action="store_true")
    args = parser.parse_args()
    image = open(args.image, "rb").read()
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.skipdata = True
    endpoint_offsets = ("+ 0x7c]", "+ 0x80]", "+ 0x84]", "+ 0x88]", "+ 0x8c]")
    tail_offsets = ("+ 0x98]", "+ 0x9c]", "+ 0xa0]", "+ 0xa4]")
    for section_name, address, code in executable_sections(image, args.mapped):
        instructions = list(decoder.disasm(code, address))
        for index, instruction in enumerate(instructions):
            if not args.min_address <= instruction.address < args.max_address:
                continue
            if args.tail_accesses and any(offset in instruction.op_str for offset in tail_offsets):
                print(f"{section_name} tail 0x{instruction.address:08X}: "
                      f"{instruction.mnemonic:<8} {instruction.op_str}")
            if instruction.mnemonic != "cmp" or not instruction.op_str.endswith(", 3"):
                continue
            begin = max(0, index - 14)
            end = min(len(instructions), index + 15)
            nearby = instructions[begin:end]
            if not any(any(offset in item.op_str for offset in endpoint_offsets) for item in nearby):
                continue
            print(f"\n{section_name} candidate 0x{instruction.address:08X}: "
                  f"{instruction.mnemonic} {instruction.op_str}")
            for item in nearby:
                marker = ">" if item.address == instruction.address else " "
                print(f"{marker} 0x{item.address:08X}  {item.mnemonic:<8} {item.op_str}")


if __name__ == "__main__":
    main()
