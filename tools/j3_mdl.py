#!/usr/bin/env python3
"""SH3 .mdl parser (J3 item b). Layout from static analysis of sh3.exe plus
column statistics over all 157 extracted .mdl files, 2026-11-12.

Outer file (verified on all 157 files):
  +0x00 u32 0 | u32 id | u32 nparts | u32 size | u32 size | u32 dataoff | 0 | 1
  +0x40 part table: nparts x u32 offsets (all == dataoff), then nparts x u32
  sizes (all == size). dataoff = round_up(0x40 + 8*nparts, 0x10).
  `size` is the ABSOLUTE file offset where the model block ends: the
  `ffffffff` texture container (see j3_arc.py) starts exactly at `size`, or
  the file ends there. Model block length = size - dataoff.

Model block directory (fixed 0x80-byte header, verified on all 157 files;
offsets are self-relative to the model block base):
  +0x00 u32 magic ffff0003   +0x04 u32 version (3)
  +0x08 offA  +0x0c nA   A: nA x 0x40 bind-pose bone matrices (row-major 4x4,
                         last row = translation + 1.0). offA == 0x80 always.
  +0x10 offB  +0x14 nB   B: nB parent bytes (0xff = root, else parent index).
  +0x18 offC  +0x1c cEnd C: sorted pairs of bone-index bytes; region [offC,cEnd).
                         M: [cEnd, offF) = nB x 0x40 float4x4 matrices (second
                         matrix set; size relation verified on all 157 files;
                         semantics inferred).
  +0x20 nD    +0x24 offD D: nD x 0x40 per-part (mesh) descriptor records;
                         field0 = total size of the D region (verified bg_bld).
  +0x28 nE    +0x2c offE E: nE x 0x40 records; region runs to model end.
  +0x30 nF    +0x34 offF F: nF u32; region [offF, offG).
  +0x38 nG    +0x3c offG G: nG x 8-byte records; region [offG, next).
  +0x40 0 (always)       +0x44 offH  H: pointer into A/B region (0 in 115/157).
  +0x48 offI  +0x4c nI   I: vertex stream, 12-byte records = 6 x s16
                         (pos.x,pos.y,pos.z, nx,ny,nz), normals scale 4096.
                         nI = number of J stream descriptors.
  +0x50 offJ  +0x54 0    J: nI x (u32 count, u32 offset) stream descriptors;
  +0x58 offK  +0x5c offK (duplicate of +0x58, always equal)
                         streams live in [offJ + 8*nI, offK), each count x 14
                         bytes (7 x s16). MORPH TARGETS (solved 2026-11-13,
                         consumer FUN_00463bd0): stream k = morph target k,
                         weight w = [charobj+0xc][k] float; record (f0..f6):
                         f6 = vertex index into I stream; pos[f6] +=
                         (f0,f1,f2)*w/16; normal[f6] += (f3,f4,f5)*w/4096
                         (runtime stages f3..f5 negated and the consumer
                         subtracts; net effect is + as shown).
  +0x60 offL  +0x64 flags  offL == offK + 0x10. flags seen: 1, 0x40000001,
                         0x40000005, 5.
  +0x68..+0x70 0 always  +0x74 f74 (0 in 148/157, 1 in 9)  +0x78,+0x7c 0.
  Region ends: each region runs to the next region start (sorted offsets);
  K region = [offK, offD), E region = [offE, model size).

Usage: j3_mdl.py FILE.mdl            dump one file
       j3_mdl.py --survey DIR...     assert + shape counts over *.mdl
"""
import os
import struct
import sys


class MdlError(Exception):
    pass


def round_up(v, a):
    return (v + a - 1) // a * a


def parse_outer(b):
    z, mid, nparts, size, size2, dataoff, z2, one = struct.unpack_from("<8I", b, 0)
    if z != 0 or one != 1 or size != size2:
        raise MdlError("unexpected outer header %r" % ((z, mid, nparts, size, size2, dataoff, z2, one),))
    exp = round_up(0x40 + 8 * nparts, 0x10)
    if dataoff != exp:
        raise MdlError("dataoff %#x != round_up(0x40+8*nparts,16) %#x" % (dataoff, exp))
    offs = struct.unpack_from("<%dI" % nparts, b, 0x40)
    szs = struct.unpack_from("<%dI" % nparts, b, 0x40 + 4 * nparts)
    if any(o != dataoff for o in offs) or any(s != size for s in szs):
        raise MdlError("part table not uniform: %r %r" % (offs, szs))
    return dict(id=mid, nparts=nparts, size=size, dataoff=dataoff)


SLOTS = [  # (name, off_count, off_ptr) in directory order; None count = no count field
    ("A", 0x0c, 0x08), ("B", 0x14, 0x10), ("C", 0x1c, 0x18),
    ("D", 0x20, 0x24), ("E", 0x28, 0x2c), ("F", 0x30, 0x34), ("G", 0x38, 0x3c),
    ("H", None, 0x44), ("I", 0x4c, 0x48), ("J", None, 0x50),
    ("K", None, 0x58), ("L", None, 0x60),
]


def parse_model(b, base, size):
    m = struct.unpack_from("<2I", b, base)
    if m[0] != 0xFFFF0003:
        raise MdlError("bad model magic %#010x at %#x" % (m[0], base))
    d = dict(base=base, size=size, version=m[1])
    g = lambda o: struct.unpack_from("<I", b, base + o)[0]
    for name, co, po in SLOTS:
        d[name] = g(po)
        if co is not None:
            d[name + "_n"] = g(co)
    d["flags"] = g(0x64)
    d["f74"] = g(0x74)
    if d["A"] != 0x80:
        raise MdlError("offA %#x != 0x80" % d["A"])
    if d["K"] != g(0x5c):
        raise MdlError("K duplicate slot mismatch")
    if d["L"] != d["K"] + 0x10:
        raise MdlError("offL %#x != offK+0x10 %#x" % (d["L"], d["K"] + 0x10))
    for o in (0x40, 0x54, 0x68, 0x6c, 0x70, 0x78, 0x7c):
        if g(o) != 0:
            raise MdlError("slot at +%#x not zero: %#x" % (o, g(o)))
    # region map: each region runs to the next start
    starts = sorted({0x80, d["A"], d["B"], d["C"], d["C_n"], d["F"], d["G"],
                     d["I"], d["J"], d["K"], d["D"], d["E"], size})
    d["starts"] = starts
    return d


def regions(b, d):
    """Named byte-complete region list [(start, end, name)] for the model block."""
    base, size = d["base"], d["size"]
    jend = d["J"] + 8 * d["I_n"]
    pts = sorted({0x80, d["A"], d["B"], d["C"], d["C_n"], d["F"], d["G"], d["I"],
                  d["J"], jend, d["K"], d["D"], d["E"], size})
    names = []
    for i in range(len(pts) - 1):
        s, e = pts[i], pts[i + 1]
        if s == e:
            continue
        def inr(x):
            return s <= x < e
        if inr(d["A"]):
            n = "A_matrices"
        elif inr(d["B"]):
            n = "B_parents"
        elif inr(d["C"]):
            n = "C_bonepairs"
        elif s >= d["C_n"] and s < d["F"]:
            n = "M_matrices"
        elif inr(d["F"]):
            n = "F_u32"
        elif inr(d["G"]):
            n = "G_recs"
        elif inr(d["I"]):
            n = "I_vertices"
        elif inr(d["J"]):
            n = "J_streamtable"
        elif s >= jend and s < d["K"]:
            n = "J_streams"
        elif inr(d["K"]):
            n = "K_zeros"
        elif inr(d["D"]):
            n = "D_parts"
        elif inr(d["E"]):
            n = "E_tail"
        else:
            n = "?"
        names.append((s, e, n))
    return names


def survey(paths):
    tot = 0
    shapes = {}
    for p in paths:
        b = open(p, "rb").read()
        o = parse_outer(b)
        end = o["size"]
        if end == len(b):
            tail = "EOF"
        elif b[end:end + 4] == b"\xff\xff\xff\xff":
            tail = "ffffffff"
        else:
            raise MdlError("%s: model block end %#x not at ffffffff (%r) or EOF (%#x)"
                           % (p, end, b[end:end + 4], len(b)))
        d = parse_model(b, o["dataoff"], o["size"] - o["dataoff"])
        # every byte of the model block assigned?
        cov = regions(b, d)
        pos = 0x80
        for s, e, n in cov:
            if n == "?" or s != pos:
                raise MdlError("%s: gap/unnamed at %#x (%r)" % (p, pos, (s, e, n)))
            pos = e
        if pos != d["size"]:
            raise MdlError("%s: regions end %#x != size %#x" % (p, pos, d["size"]))
        shape = tuple(1 if d[n] else 0 for n, _, _ in SLOTS) + (d["flags"],)
        shapes[shape] = shapes.get(shape, 0) + 1
        tot += 1
        print("%-40s parts=%d size=%#x tail=%s nA=%#x nD=%#x nI=%#x flags=%#x" %
              (os.path.basename(p), o["nparts"], o["size"], tail,
               d["A_n"], d["D_n"], d["I_n"], d["flags"]))
    print("files: %d, all model blocks end at the ffffffff container or EOF" % tot)
    print("directory shapes (nonzero-slot pattern + flags): %d distinct" % len(shapes))
    for sh, c in sorted(shapes.items(), key=lambda kv: -kv[1]):
        names = [n for (n, _, _), v in zip(SLOTS, sh[:len(SLOTS)]) if v]
        print("  %3d x  slots=%s flags=%#x" % (c, ",".join(names), sh[-1]))


def dump(p):
    b = open(p, "rb").read()
    o = parse_outer(b)
    print("outer:", o)
    d = parse_model(b, o["dataoff"], o["size"] - o["dataoff"])
    for k in ("A", "A_n", "B", "B_n", "C", "C_n", "D", "D_n", "E", "E_n", "F", "F_n",
              "G", "G_n", "H", "I", "I_n", "J", "K", "L", "flags", "f74"):
        print("  %-5s %#x" % (k, d[k]))
    for s, e, n in regions(b, d):
        print("  region %#06x..%#06x (%#x) %s" % (s, e, e - s, n))


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--survey":
        paths = []
        for p in a[1:]:
            if os.path.isdir(p):
                paths += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith(".mdl"))
            else:
                paths.append(p)
        survey(paths)
    else:
        dump(a[0])
