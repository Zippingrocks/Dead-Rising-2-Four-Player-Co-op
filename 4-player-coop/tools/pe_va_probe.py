"""Dump DWORDs/instructions at PE32 VAs, or find absolute VA references."""

import struct
import sys

import capstone


mapped = len(sys.argv) > 1 and sys.argv[1] == "--mapped"
path_index = 2 if mapped else 1
data = open(sys.argv[path_index], "rb").read()
base = 0x400000
pe = struct.unpack_from("<I", data, 0x3C)[0]
optional_size = struct.unpack_from("<H", data, pe + 20)[0]
section_count = struct.unpack_from("<H", data, pe + 6)[0]
sections = [struct.unpack_from("<IIII", data, pe + 24 + optional_size + i * 40 + 8) for i in range(section_count)]


def file_offset(va):
    rva = va - base
    if mapped:
        return rva
    for virtual_size, virtual_address, raw_size, raw_address in sections:
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_address + rva - virtual_address
    raise ValueError(hex(va))


decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for raw in sys.argv[path_index + 1:]:
    if raw.startswith("string:"):
        needle = raw[7:].encode("ascii") + b"\0"
        offsets = []
        start = 0
        while True:
            offset = data.find(needle, start)
            if offset < 0:
                break
            offsets.append(offset)
            start = offset + 1
        if not offsets:
            print(f"\nstring {raw[7:]!r}: not found")
            continue
        addresses = []
        for offset in offsets:
            address = None
            if mapped:
                address = base + offset
            else:
                for virtual_size, virtual_address, raw_size, raw_address in sections:
                    if raw_address <= offset < raw_address + raw_size:
                        address = base + virtual_address + offset - raw_address
                        break
            addresses.append(f"{address:#010x}" if address else f"file:{offset:#x}")
        print(f"\nstring {raw[7:]!r}: " + " ".join(addresses))
        continue
    if raw.startswith("xref:"):
        target = int(raw[5:], 0)
        needle = struct.pack("<I", target)
        matches = []
        start = 0
        while True:
            offset = data.find(needle, start)
            if offset < 0:
                break
            if mapped:
                matches.append(base + offset)
            else:
                for virtual_size, virtual_address, raw_size, raw_address in sections:
                    if raw_address <= offset < raw_address + raw_size:
                        matches.append(base + virtual_address + offset - raw_address)
                        break
            start = offset + 1
        print(f"\nxrefs to {target:#010x}: " + (" ".join(hex(match) for match in matches) or "none"))
        continue
    if raw.startswith("callxref:"):
        target = int(raw[9:], 0)
        matches = []
        for virtual_size, virtual_address, raw_size, raw_address in sections:
            section_offset = virtual_address if mapped else raw_address
            section_size = virtual_size if mapped else raw_size
            section = data[section_offset : section_offset + section_size]
            for offset in range(max(0, len(section) - 4)):
                if section[offset] != 0xE8:
                    continue
                source = base + virtual_address + offset
                relative = struct.unpack_from("<i", section, offset + 1)[0]
                if source + 5 + relative == target:
                    matches.append(source)
        print(f"\ncalls to {target:#010x}: " + (" ".join(hex(match) for match in matches) or "none"))
        continue
    if raw.startswith("pattern:"):
        encoded = raw[8:].replace(" ", "")
        if len(encoded) % 2:
            raise ValueError("pattern must contain complete byte pairs")
        pattern = [None if encoded[i : i + 2] in ("??", "**") else int(encoded[i : i + 2], 16)
                   for i in range(0, len(encoded), 2)]
        matches = []
        for virtual_size, virtual_address, raw_size, raw_address in sections:
            section_offset = virtual_address if mapped else raw_address
            section_size = virtual_size if mapped else raw_size
            section = data[section_offset : section_offset + section_size]
            for offset in range(max(0, len(section) - len(pattern) + 1)):
                if all(expected is None or section[offset + i] == expected for i, expected in enumerate(pattern)):
                    matches.append(base + virtual_address + offset)
        print(f"\npattern {encoded}: " + (" ".join(hex(match) for match in matches) or "none"))
        continue
    address = int(raw, 0)
    offset = file_offset(address)
    words = struct.unpack_from("<8I", data, offset)
    print(f"\n{address:#010x} dwords: " + " ".join(f"{word:08X}" for word in words))
    for instruction in decoder.disasm(data[offset : offset + 128], address):
        print(f"  {instruction.address:08X}: {instruction.mnemonic:<7} {instruction.op_str}")
