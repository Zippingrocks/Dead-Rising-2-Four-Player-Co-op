"""Disassemble vanilla DR2 call sites for Steam's callback registration API."""

import re
import struct
import sys

import capstone


exe = open(sys.argv[1], "rb").read()
base = 0x400000
pe = struct.unpack_from("<I", exe, 0x3C)[0]
optional_size = struct.unpack_from("<H", exe, pe + 20)[0]
section_count = struct.unpack_from("<H", exe, pe + 6)[0]
sections = [struct.unpack_from("<IIII", exe, pe + 24 + optional_size + i * 40 + 8) for i in range(section_count)]


def file_offset(rva):
    for virtual_size, virtual_address, raw_size, raw_address in sections:
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_address + rva - virtual_address
    raise ValueError(hex(rva))


def c_string(offset):
    return exe[offset : exe.index(b"\0", offset)].decode()


imports = {}
descriptor = file_offset(struct.unpack_from("<I", exe, pe + 24 + 104)[0])
while True:
    original, _timestamp, _chain, name_rva, first = struct.unpack_from("<5I", exe, descriptor)
    if not name_rva:
        break
    if c_string(file_offset(name_rva)).lower() == "steam_api.dll":
        index = 0
        while True:
            value = struct.unpack_from("<I", exe, file_offset(original) + index * 4)[0]
            if not value:
                break
            if not value & 0x80000000:
                imports[c_string(file_offset(value) + 2)] = base + first + index * 4
            index += 1
    descriptor += 20

targets = {
    name: address
    for name, address in imports.items()
    if name
    in {
        "SteamAPI_RegisterCallResult",
        "SteamAPI_UnregisterCallResult",
        "SteamAPI_RegisterCallback",
        "SteamAPI_UnregisterCallback",
        "SteamAPI_RunCallbacks",
    }
}
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
text_rva = sections[0][1]
text = exe[file_offset(text_rva) : file_offset(text_rva) + sections[0][0]]

for name, iat in sorted(targets.items()):
    pattern = b"\xff\x15" + struct.pack("<I", iat)
    sites = [base + text_rva + match.start() for match in re.finditer(re.escape(pattern), text)]
    print(f"\n{name} IAT={iat:#010x} sites={len(sites)}")
    for site in sites:
        start = max(base + text_rva, site - 40)
        stop = site + 64
        code = exe[file_offset(start - base) : file_offset(start - base) + stop - start]
        print(f"\n  call site {site:#010x}")
        for instruction in decoder.disasm(code, start):
            marker = "=>" if instruction.address == site else "  "
            print(f"  {marker} {instruction.address:08X}: {instruction.mnemonic:<7} {instruction.op_str}")
