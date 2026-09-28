# Dump enums and class/struct member layouts from an MSF 7.00 PDB's TPI stream.
# Usage: python pdb_types.py <file.pdb> <enums.txt> <structs.txt>
import struct
import sys

pdb = open(sys.argv[1], "rb").read()
block_size, _free, _num_blocks, dir_bytes, _unk, block_map = struct.unpack_from("<6I", pdb, 32)


def read_blocks(count_bytes, index_list):
    return b"".join(pdb[b * block_size:(b + 1) * block_size] for b in index_list)[:count_bytes]


dir_blocks = struct.unpack_from("<%dI" % ((dir_bytes + block_size - 1) // block_size), pdb, block_map * block_size)
directory = read_blocks(dir_bytes, dir_blocks)
num_streams = struct.unpack_from("<I", directory, 0)[0]
sizes = struct.unpack_from("<%dI" % num_streams, directory, 4)
pos = 4 + 4 * num_streams


def stream(index):
    p = 4 + 4 * num_streams
    for i, size in enumerate(sizes):
        n = 0 if size == 0xFFFFFFFF else (size + block_size - 1) // block_size
        if i == index:
            return read_blocks(size, struct.unpack_from("<%dI" % n, directory, p))
        p += 4 * n


tpi = stream(2)
header_size, ti_begin, ti_end, record_bytes = struct.unpack_from("<IIII", tpi, 4)
types = {}
p = header_size
ti = ti_begin
while p < header_size + record_bytes:
    length, = struct.unpack_from("<H", tpi, p)
    types[ti] = tpi[p + 2:p + 2 + length]
    p += length + 2
    ti += 1


def numeric(data, p):
    value, = struct.unpack_from("<H", data, p)
    if value < 0x8000:
        return value, p + 2
    fmt = {0x8000: "<b", 0x8001: "<h", 0x8002: "<H", 0x8003: "<i", 0x8004: "<I", 0x8009: "<q", 0x800A: "<Q"}[value]
    return struct.unpack_from(fmt, data, p + 2)[0], p + 2 + struct.calcsize(fmt)


def cstr(data, p):
    end = data.index(b"\0", p)
    return data[p:end].decode("latin1"), end + 1


def align(data, p):
    while p < len(data) and data[p] >= 0xF0:
        p += data[p] & 0x0F
    return p


prims = {0x74: "int", 0x75: "uint", 0x10: "char", 0x20: "uchar", 0x11: "short", 0x21: "ushort", 0x40: "float",
         0x41: "double", 0x30: "bool", 0x03: "void", 0x12: "long", 0x22: "ulong", 0x13: "int64", 0x23: "uint64", 0x70: "char"}


def type_name(t, depth=0):
    if t < 0x1000:
        base = prims.get(t & 0xFF, "prim%X" % t)
        return base + ("*" if t & 0xF00 else "")
    rec = types.get(t)
    if rec is None or depth > 6:
        return "T%X" % t
    kind, = struct.unpack_from("<H", rec, 0)
    if kind in (0x1504, 0x1505, 0x1506):
        _, p = numeric(rec, 10 if kind == 0x1506 else 18)
        return cstr(rec, p)[0]
    if kind == 0x1507:
        return cstr(rec, 14)[0]
    if kind == 0x1002:  # LF_POINTER
        return type_name(struct.unpack_from("<I", rec, 2)[0], depth + 1) + "*"
    if kind == 0x1001:  # LF_MODIFIER
        return type_name(struct.unpack_from("<I", rec, 2)[0], depth + 1)
    if kind == 0x1503:  # LF_ARRAY
        elem, _idx = struct.unpack_from("<II", rec, 2)
        size, _ = numeric(rec, 10)
        return "%s[bytes=%d]" % (type_name(elem, depth + 1), size)
    return "T%X" % t


def field_list(t):
    rec = types.get(t)
    out = []
    if rec is None:
        return out
    p = 2
    while p < len(rec):
        kind, = struct.unpack_from("<H", rec, p)
        if kind == 0x1502:  # LF_ENUMERATE
            value, q = numeric(rec, p + 4)
            name, q = cstr(rec, q)
            out.append(("enum", name, value))
        elif kind == 0x150D:  # LF_MEMBER
            _attr, mtype = struct.unpack_from("<HI", rec, p + 2)
            offset, q = numeric(rec, p + 8)
            name, q = cstr(rec, q)
            out.append(("member", name, offset, type_name(mtype)))
        elif kind == 0x1400:  # LF_BCLASS
            _attr, btype = struct.unpack_from("<HI", rec, p + 2)
            offset, q = numeric(rec, p + 8)
            out.append(("base", type_name(btype), offset))
        elif kind == 0x150E:  # LF_STMEMBER
            _attr, mtype = struct.unpack_from("<HI", rec, p + 2)
            name, q = cstr(rec, p + 8)
            out.append(("static", name, type_name(mtype)))
        elif kind == 0x1409:  # LF_VFUNCTAB
            q = p + 8
        elif kind == 0x150F:  # LF_METHOD
            name, q = cstr(rec, p + 8)
        elif kind == 0x1511:  # LF_ONEMETHOD
            attr, = struct.unpack_from("<H", rec, p + 2)
            q = p + 8 + (4 if ((attr >> 2) & 7) in (4, 6) else 0)
            name, q = cstr(rec, q)
        elif kind == 0x1510:  # LF_NESTTYPE
            name, q = cstr(rec, p + 8)
        elif kind == 0x1401:  # LF_VBCLASS / LF_IVBCLASS
            _, q = numeric(rec, p + 12)
            _, q = numeric(rec, q)
        elif kind == 0x1402:
            _, q = numeric(rec, p + 12)
            _, q = numeric(rec, q)
        elif kind == 0x1404:  # LF_INDEX
            out.extend(field_list(struct.unpack_from("<I", rec, p + 4)[0]))
            break
        else:
            out.append(("unknown", "0x%X" % kind))
            break
        p = align(rec, q)
    return out


seen_enum = set()
seen_struct = set()
with open(sys.argv[2], "w", encoding="utf-8") as enums, open(sys.argv[3], "w", encoding="utf-8") as structs:
    for t, rec in types.items():
        kind, = struct.unpack_from("<H", rec, 0)
        if kind == 0x1507:
            count, prop, _utype, fields = struct.unpack_from("<HHII", rec, 2)
            name = cstr(rec, 14)[0]
            if prop & 0x80 or name in seen_enum:  # forward reference
                continue
            seen_enum.add(name)
            enums.write("enum %s\n" % name)
            for item in field_list(fields):
                if item[0] == "enum":
                    enums.write("  %s = %d\n" % (item[1], item[2]))
        elif kind in (0x1504, 0x1505):
            count, prop, fields, _derived, _vshape = struct.unpack_from("<HHIII", rec, 2)
            size, p = numeric(rec, 18)
            name = cstr(rec, p)[0]
            if prop & 0x80 or name in seen_struct:
                continue
            seen_struct.add(name)
            structs.write("struct %s size=0x%X\n" % (name, size))
            for item in field_list(fields):
                if item[0] == "member":
                    structs.write("  +0x%X %s : %s\n" % (item[2], item[1], item[3]))
                elif item[0] == "base":
                    structs.write("  base %s @0x%X\n" % (item[1], item[2]))
                elif item[0] == "static":
                    structs.write("  static %s : %s\n" % (item[1], item[2]))
print("types", len(types), "enums", len(seen_enum), "structs", len(seen_struct))
