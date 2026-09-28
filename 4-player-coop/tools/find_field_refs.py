"""Find x86 instructions whose memory operand uses one of the requested displacements.

Usage: python find_field_refs.py <pe32-image> <disp> [<disp> ...]
"""

import struct
import sys

import capstone
from capstone.x86 import X86_OP_MEM


if len(sys.argv) < 3:
    raise SystemExit("usage: find_field_refs.py <pe32-image> <disp> [<disp> ...]")

path = sys.argv[1]
targets = {int(value, 0) for value in sys.argv[2:]}
data = open(path, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
section_count = struct.unpack_from("<H", data, pe + 6)[0]
optional_size = struct.unpack_from("<H", data, pe + 20)[0]
image_base = struct.unpack_from("<I", data, pe + 52)[0]
section_table = pe + 24 + optional_size

decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
decoder.detail = True
decoder.skipdata = True

for index in range(section_count):
    header = section_table + index * 40
    name = data[header:header + 8].rstrip(b"\0").decode("ascii", errors="replace")
    virtual_size, virtual_address, raw_size, raw_address = struct.unpack_from("<IIII", data, header + 8)
    characteristics = struct.unpack_from("<I", data, header + 36)[0]
    if not (characteristics & 0x20000000) or not raw_size:
        continue
    code = data[raw_address:raw_address + raw_size]
    start = image_base + virtual_address
    chunk_size = 0x100000
    overlap = 15
    for chunk_offset in range(0, len(code), chunk_size):
        chunk = code[chunk_offset:min(len(code), chunk_offset + chunk_size + overlap)]
        chunk_end = start + min(len(code), chunk_offset + chunk_size)
        for instruction in decoder.disasm(chunk, start + chunk_offset):
            if instruction.address >= chunk_end or instruction.id == 0:
                continue
            hits = sorted({operand.mem.disp for operand in instruction.operands
                           if operand.type == X86_OP_MEM and operand.mem.disp in targets})
            if hits:
                rendered = ",".join(f"{hit:#x}" for hit in hits)
                print(f"{instruction.address:08X}\t{name}\t{rendered}\t"
                      f"{instruction.mnemonic:<7}\t{instruction.op_str}")
