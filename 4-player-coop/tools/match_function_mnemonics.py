"""Find exact mnemonic-sequence matches for a function across two PE32 images.

Usage: python match_function_mnemonics.py <source-pe> <source-va> <instruction-count> <target-pe>
       [target-start] [target-end] [--target-mapped]
"""

import struct
import sys
import heapq
import difflib

import capstone


def pe_sections(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<I", data, pe + 52)[0]
    table = pe + 24 + optional_size
    sections = []
    for index in range(count):
        header = table + index * 40
        virtual_size, virtual_address, raw_size, raw_address = struct.unpack_from("<IIII", data, header + 8)
        characteristics = struct.unpack_from("<I", data, header + 36)[0]
        sections.append((virtual_address, virtual_size, raw_address, raw_size, characteristics))
    return data, image_base, sections


def bytes_at_va(data, base, sections, va, size):
    rva = va - base
    for virtual_address, virtual_size, raw_address, raw_size, _ in sections:
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            offset = raw_address + rva - virtual_address
            return data[offset:offset + size]
    raise ValueError(hex(va))


source_path, source_va_text, count_text, target_path = sys.argv[1:5]
target_mapped = "--target-mapped" in sys.argv[5:]
range_args = [arg for arg in sys.argv[5:] if arg != "--target-mapped"]
target_start = int(range_args[0], 0) if len(range_args) > 0 else 0
target_end = int(range_args[1], 0) if len(range_args) > 1 else 0xFFFFFFFF
source_va = int(source_va_text, 0)
instruction_count = int(count_text, 0)
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

source_data, source_base, source_sections = pe_sections(source_path)
source_instructions = list(decoder.disasm(bytes_at_va(source_data, source_base, source_sections, source_va, 0x400), source_va))
sequence = tuple(instruction.mnemonic for instruction in source_instructions[:instruction_count])
print("source:", " ".join(sequence))

target_data, target_base, target_sections = pe_sections(target_path)
for virtual_address, _virtual_size, raw_address, raw_size, characteristics in target_sections:
    if not (characteristics & 0x20000000) or not raw_size:
        continue
    if target_mapped:
        code_size = min(max(_virtual_size, raw_size), len(target_data) - virtual_address)
        code = target_data[virtual_address:virtual_address + code_size]
    else:
        code = target_data[raw_address:raw_address + raw_size]
    decoder.skipdata = True
    instructions = [instruction for instruction in decoder.disasm(code, target_base + virtual_address) if instruction.id]
    mnemonics = [instruction.mnemonic for instruction in instructions]
    ranked = []
    fuzzy_ranked = []
    for index in range(0, len(instructions) - len(sequence) + 1):
        if not target_start <= instructions[index].address < target_end:
            continue
        if tuple(mnemonics[index:index + len(sequence)]) == sequence:
            print(f"match: {instructions[index].address:#010x}")
        if mnemonics[index] != sequence[0]:
            continue
        score = sum(left == right for left, right in zip(sequence, mnemonics[index:index + len(sequence)]))
        item = (score, instructions[index].address)
        if len(ranked) < 12:
            heapq.heappush(ranked, item)
        elif item > ranked[0]:
            heapq.heapreplace(ranked, item)
        window = mnemonics[index:index + len(sequence) + 8]
        ratio = difflib.SequenceMatcher(None, sequence, window, autojunk=False).ratio()
        fuzzy_item = (ratio, instructions[index].address)
        if len(fuzzy_ranked) < 12:
            heapq.heappush(fuzzy_ranked, fuzzy_item)
        elif fuzzy_item > fuzzy_ranked[0]:
            heapq.heapreplace(fuzzy_ranked, fuzzy_item)
    for score, address in sorted(ranked, reverse=True):
        print(f"near: {address:#010x} positional={score}/{len(sequence)}")
    for ratio, address in sorted(fuzzy_ranked, reverse=True):
        print(f"fuzzy: {address:#010x} ratio={ratio:.3f}")
