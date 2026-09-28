# Dump public symbols (S_PUB32) and section headers from an MSF 7.00 PDB.
# Usage: python pdb_publics.py <file.pdb> <out.tsv>
# Output columns: VA (image base 0x400000 + section RVA + offset), section, offset, name.
import struct
import sys

pdb = open(sys.argv[1], "rb").read()
assert pdb.startswith(b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"), "not an MSF 7.00 PDB"
block_size, _free, num_blocks, dir_bytes, _unk, block_map = struct.unpack_from("<6I", pdb, 32)


def blocks(count_bytes, index_list):
    return b"".join(pdb[b * block_size:(b + 1) * block_size] for b in index_list)[:count_bytes]


dir_block_count = (dir_bytes + block_size - 1) // block_size
dir_blocks = struct.unpack_from("<%dI" % dir_block_count, pdb, block_map * block_size)
directory = blocks(dir_bytes, dir_blocks)
num_streams = struct.unpack_from("<I", directory, 0)[0]
sizes = struct.unpack_from("<%dI" % num_streams, directory, 4)
pos = 4 + 4 * num_streams
streams = []
for size in sizes:
    if size == 0xFFFFFFFF:
        streams.append(b"")
        continue
    n = (size + block_size - 1) // block_size
    idx = struct.unpack_from("<%dI" % n, directory, pos)
    pos += 4 * n
    streams.append(blocks(size, idx))

dbi = streams[3]
(sig, ver, age, gsi, bld, psi, pdbdll, symrec, rbld, modsz, secconsz, secmapsz, fileinfosz, tsmsz, mfc,
 dbgsz, ecsz, flags, machine, pad) = struct.unpack_from("<iIIHHHHHHiiiiiIiiHHI", dbi, 0)
# Optional debug header: stream index of the section headers (index 5).
dbg = struct.unpack_from("<11H", dbi, 64 + modsz + secconsz + secmapsz + fileinfosz + tsmsz + ecsz)
sections = streams[dbg[5]]
rvas = [struct.unpack_from("<I", sections, i * 40 + 12)[0] for i in range(len(sections) // 40)]

records = streams[symrec]
out = open(sys.argv[2], "w", encoding="utf-8")
out.write("va\tsection\toffset\tname\n")
count = 0
p = 0
while p + 4 <= len(records):
    length, kind = struct.unpack_from("<HH", records, p)
    if kind == 0x110E:  # S_PUB32
        flags, offset, segment = struct.unpack_from("<IIH", records, p + 4)
        name = records[p + 14:records.index(b"\0", p + 14)].decode("latin1")
        va = 0x400000 + rvas[segment - 1] + offset if 0 < segment <= len(rvas) else 0
        out.write("0x%08X\t%d\t0x%X\t%s\n" % (va, segment, offset, name))
        count += 1
    p += length + 2
print("publics:", count)
