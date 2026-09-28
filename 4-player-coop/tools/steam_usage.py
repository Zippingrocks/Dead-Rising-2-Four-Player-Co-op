# List which Steamworks interface methods DR2 calls, by scanning call sites of the steam_api accessor imports
# (SteamMatchmaking(), SteamNetworking(), ...) in the unpacked vanilla executable and resolving the following
# virtual call's vtable slot. Usage: python steam_usage.py <unpacked exe> <sdk header dir>
import collections
import re
import struct
import sys

import capstone

exe = open(sys.argv[1], "rb").read()
sdk = sys.argv[2]
BASE = 0x400000
pe = struct.unpack_from("<I", exe, 0x3C)[0]
osz = struct.unpack_from("<H", exe, pe + 20)[0]
nsec = struct.unpack_from("<H", exe, pe + 6)[0]
secs = [struct.unpack_from("<IIII", exe, pe + 24 + osz + i * 40 + 8) for i in range(nsec)]


def fo(rva):
    for vs, va, rs, raw in secs:
        if va <= rva < va + max(vs, rs):
            return raw + rva - va


def cstr(o):
    return exe[o:exe.index(b"\0", o)].decode()


# Import slots of steam_api.dll: name -> IAT slot VA.
slots = {}
p = fo(struct.unpack_from("<I", exe, pe + 24 + 104)[0])
while True:
    oft, _ts, _fc, name, ft = struct.unpack_from("<5I", exe, p)
    if name == 0:
        break
    if cstr(fo(name)).lower() == "steam_api.dll":
        i = 0
        while True:
            t = struct.unpack_from("<I", exe, fo(oft) + 4 * i)[0]
            if t == 0:
                break
            if not t & 0x80000000:
                slots[BASE + ft + 4 * i] = cstr(fo(t) + 2)
            i += 1
    p += 20


def methods(header, cls):
    s = open("%s/%s" % (sdk, header), encoding="latin1").read()
    body = re.search(r"class %s\s*\{(.*?)\n\};" % cls, s, re.S).group(1)
    body = re.sub(r"#ifdef _PS3.*?#endif", "", re.sub(r"//.*", "", body), flags=re.S)
    return re.findall(r"virtual\s+[^;(]*?\b(\w+)\s*\(", body)


tables = {
    "SteamMatchmaking": methods("isteammatchmaking.h", "ISteamMatchmaking"),
    "SteamNetworking": methods("isteamnetworking.h", "ISteamNetworking"),
    "SteamUser": methods("isteamuser.h", "ISteamUser"),
    "SteamFriends": methods("isteamfriends.h", "ISteamFriends"),
    "SteamUtils": methods("isteamutils.h", "ISteamUtils"),
}

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = False
text_raw = fo(0x1000)
text = exe[text_raw:text_raw + secs[0][0]]
usage = collections.defaultdict(lambda: collections.defaultdict(list))
direct = collections.defaultdict(list)
for m in re.finditer(rb"\xff\x15(....)", text, re.S):
    slot = struct.unpack("<I", m.group(1))[0]
    if slot not in slots:
        continue
    api = slots[slot]
    site = BASE + 0x1000 + m.start()
    direct[api].append(site)
    if api not in tables:
        continue
    # Follow the returned interface pointer (eax) through register moves to the first virtual call.
    ptr_regs = {"eax"}
    vt_regs = set()
    for ins in md.disasm(text[m.start() + 6:m.start() + 6 + 160], site + 6):
        ops = [o.strip() for o in ins.op_str.split(",")]
        if ins.mnemonic == "mov" and len(ops) == 2:
            dst, src = ops
            mem = re.fullmatch(r"dword ptr \[(\w+)\]", src)
            if mem and mem.group(1) in ptr_regs:
                vt_regs.add(dst)
                continue
            if src in ptr_regs:
                ptr_regs.add(dst)
                continue
            mem = re.fullmatch(r"dword ptr \[(\w+) \+ (0x[0-9a-f]+|\d+)\]", src)
            if mem and mem.group(1) in vt_regs:
                usage[api][int(mem.group(2), 0) // 4].append(site)
                break
            ptr_regs.discard(dst)
            vt_regs.discard(dst)
        if ins.mnemonic == "call":
            mem = re.fullmatch(r"dword ptr \[(\w+)(?: \+ (0x[0-9a-f]+|\d+))?\]", ins.op_str)
            if mem and mem.group(1) in vt_regs:
                usage[api][int(mem.group(2) or "0", 0) // 4].append(site)
                break
            if ins.op_str not in vt_regs:
                break  # an unrelated call clobbers eax; give up on this site
        if ins.mnemonic in ("ret", "jmp"):
            break

print("steam_api imports:", ", ".join(sorted(set(slots.values()))))
for api in sorted(direct):
    print("\n%s(): %d call sites" % (api, len(direct[api])))
    if api in tables:
        for index in sorted(usage[api]):
            name = tables[api][index] if index < len(tables[api]) else "?"
            print("  [%2d] %-45s x%d  e.g. 0x%08X" % (index, name, len(usage[api][index]), usage[api][index][0]))
        unresolved = len(direct[api]) - sum(len(v) for v in usage[api].values())
        if unresolved:
            print("  unresolved sites: %d" % unresolved)
