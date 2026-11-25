"""Puts the Twisted Christmas Jam prop models the terrain styles use into a game dir.

    uv run --no-project python -I tests/link-tcj-props.py <tcj dir> <basedir>/id1

Loose files are symlinked; models inside the jam's pak0.pak (Arcane Dimensions) are copied out.
Nothing here is a release input: the models stay in the git-excluded run/ dir.
"""
import os
import struct
import sys

PAK_MODELS = [
    "progs/misc_xmastree.mdl",
    "progs/misc_plinth1.mdl",
    "progs/misc_plinth2.mdl",
    "progs/key_rune1.mdl",
    "progs/key_rune2.mdl",
    "progs/key_rune3.mdl",
    "progs/key_rune4.mdl",
    "progs/brk_pot1a.mdl",
    "progs/brk_pot2a.mdl",
    "progs/brk_vase1.mdl",
    "progs/misc_jstatue_02.mdl",
    "progs/misc_jstatue_03.mdl",
    "progs/misc_tutstatue.mdl",
]
LOOSE = [
    "progs/kebby/balxj_tree_a.mdl",
    "progs/kebby/balxj_tree_baubles.mdl",
    "progs/kebby/kebby_xjgift.mdl",
]


def main(tcj, dest):
    for rel in LOOSE:
        src = os.path.join(tcj, rel)
        out = os.path.join(dest, rel)
        if os.path.exists(src) and not os.path.lexists(out):
            os.makedirs(os.path.dirname(out), exist_ok=True)
            os.symlink(os.path.abspath(src), out)
    with open(os.path.join(tcj, "pak0.pak"), "rb") as f:
        _, off, length = struct.unpack("<4sii", f.read(12))
        f.seek(off)
        table = f.read(length)
        entries = {}
        for i in range(length // 64):
            name, o, n = struct.unpack("<56sii", table[i * 64 : (i + 1) * 64])
            entries[name.split(b"\0")[0].decode("latin1")] = (o, n)
        for rel in PAK_MODELS:
            if rel not in entries:
                print("missing", rel)
                continue
            o, n = entries[rel]
            f.seek(o)
            out = os.path.join(dest, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as w:
                w.write(f.read(n))
    print("props ready in", dest)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
