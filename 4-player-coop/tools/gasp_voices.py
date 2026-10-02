# Map DR2's GASP voice database (data/audio/gasp.big, extracted to research/gasp/) to speakers:
# keyword -> sound (snt) -> bucket (bkt) -> sound group (sg) -> samples (.adw IDs), with the rules (rlz) on every level
# resolved to their ActorName / characterNameID conditions. Writes research/gasp/voice_inventory.json and prints a
# per-speaker summary. Read-only analysis; names are recovered by hashing candidate strings with DR2's h*33^c hash.
import collections
import glob
import json
import os
import re
import struct

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "research", "gasp")
EXE = r"D:\.codex\.codex workspaces\DR2-Porting-Workspace\scratch\deadrising2-runtime-unpacked.exe"
AUDIO = r"C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2\data\audio"


def dr2_hash(text):
    value = 0
    for c in text.encode("latin1"):
        if c > 127:
            c -= 256
        value = ((value * 33) & 0xFFFFFFFF) ^ (c & 0xFFFFFFFF)
    return struct.unpack("<i", struct.pack("<I", value))[0]


def blocks(path):
    text = open(os.path.join(HERE, path), encoding="latin1").read()
    for block in re.findall(r"\{(.*?)\}", text, re.S):
        fields = {}
        for key, value in re.findall(r"^\s*([\w%]+)\s*=\s*(.*?)\s*$", block, re.M):
            fields.setdefault(key, []).append(value)
        yield fields


def ints(value):
    return [int(x) for x in re.findall(r"-?\d+", value.split("#")[0])]


candidates = set()
for source in [EXE, os.path.join(HERE, "datafile_text.txt")] + glob.glob(os.path.join(AUDIO, "*.txt")):
    data = open(source, "rb").read()
    for match in re.finditer(rb"[A-Za-z0-9_\-\.]{2,64}", data):
        candidates.add(match.group().decode())
names = {dr2_hash(c): c for c in candidates}

statements = {}
for b in blocks("stmts.txt"):
    parts = b["stmt"][0].split()
    statements[int(b["stid"][0])] = (int(parts[0]), [int(p) for p in parts[1:]])
rules = {int(b["rid"][0]): ints(b["stmt"][0].replace("/", ",")) for b in blocks("rules.txt")}
ACTOR, CHARID = dr2_hash("ActorName"), dr2_hash("characterNameID")


def speakers(rule_ids):
    found = set()
    for rid in rule_ids:
        for stid in rules.get(rid, []):
            if stid not in statements:
                continue
            var, args = statements[stid]
            if var == ACTOR and args and args[0] == 1:
                found.add(names.get(args[-1], "actor#%d" % args[-1]))
            elif var == CHARID and args and args[0] == 1:
                found.add("characterNameID=%d" % args[-1])
    return found


inventory = collections.defaultdict(lambda: collections.defaultdict(set))  # speaker -> keyword -> samples
for table in ("chuc", "char", "boss", "comm"):
    keywords = {}
    for line in open(os.path.join(HERE, table + "_kwd.txt"), encoding="latin1"):
        m = re.match(r"\s*(-?\d+)\s*=\s*\[([^\]]*)\]", line)
        if m:
            keywords[int(m.group(1))] = ints(m.group(2))
    sounds = {int(b["sid"][0]): b for b in blocks(table + "_snt.txt")}
    buckets = {int(b["bid"][0]): b for b in blocks(table + "_bkt.txt")}
    groups = {int(b["sid"][0]): b for b in blocks(table + "_sg.txt")}
    rl = lambda b: [r for v in b.get("rlz", []) for r in ints(v)]
    for kw_hash, sound_ids in keywords.items():
        keyword = names.get(kw_hash, "kw%d" % kw_hash)
        for sid in sound_ids:
            snt = sounds.get(sid)
            if not snt:
                continue
            for bid in ints(snt["bkt"][0]):
                bkt = buckets.get(bid)
                if not bkt:
                    continue
                for gid in ints(bkt["sgs"][0]):
                    sg = groups.get(gid)
                    if not sg:
                        continue
                    who = speakers(rl(snt) + rl(bkt) + rl(sg)) or {("Chuck" if table == "chuc" else "<any %s>" % table)}
                    for speaker in who:
                        inventory[speaker][keyword].update(ints(sg["smp"][0]))

summary = {s: {k: sorted(v) for k, v in kws.items()} for s, kws in inventory.items()}
json.dump(summary, open(os.path.join(HERE, "voice_inventory.json"), "w"), indent=1)
rows = sorted(((s, len(k), sum(len(v) for v in k.values())) for s, k in inventory.items()), key=lambda r: -r[2])
for speaker, nk, ns in rows:
    print("%-40s keywords=%3d samples=%5d  %s" % (speaker, nk, ns, ", ".join(sorted(inventory[speaker])[:12])))
