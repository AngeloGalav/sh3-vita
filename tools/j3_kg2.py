#!/usr/bin/env python3
"""SH3 .kg2 / .lv compact-geometry container (J3). Layout from the exe loop in
0x4ccd90 (called by 0x4877d0 for kg2 channels), verified on all 232 non-empty
.kg2 files (every file parses exactly to its end), 2026-11-12:

  file header (0x10 B): u16 id | u16 hi (0 -> exe derives it via 0x4cc540) |
                        u16 nobj (at +4) | ... ; objects follow at +0x10
  object (variable):
    +0x00 16 B header: u32 0 | u16 index, u16 ngroups (u32 at +4) | 0 | 0
    +0x10 16 B (unused here, read by the exe as the second header row)
    +0x20 4x4 float matrix (object transform; copied by 0x678070 into the
          0x78-byte runtime entry; rows 0..2 rotation, row 3 = world origin)
    +0x60 first group
  group (16 B slots): slot 0 = header
        u16 nslots (incl. header) | u16 hi | u16 type | u16 nslots   (u32 at +4 = nslots<<16 | type)
        then 8 B items: s16 x4 (x,y,z,1) positions and (nx,ny,nz,0) normals,
        +-32767/-32768 = +-1.0; header slot also carries (cx,cy,cz,r)
        (bounding sphere) in its last 8 bytes.
    the exe advances group to group by s16(hi16 of u32 at +4) * 16.
  group type 3 (u32 at +4 == 3) is special-cased by 0x4ccd90 (time-of-day /
  light-state code at 0xbaca60).
Usage: j3_kg2.py FILE.kg2  (prints objects/groups)
"""
import struct
import sys


def parse(b):
    n = struct.unpack_from("<H", b, 4)[0]
    off = 0x10
    objs = []
    for _ in range(n):
        h = struct.unpack_from("<4I", b, off)
        ng = h[1] >> 16
        mat = struct.unpack_from("<16f", b, off + 0x20)
        o = off + 0x60
        groups = []
        for _ in range(ng):
            gh = struct.unpack_from("<4I", b, o)
            ns = gh[1] >> 16
            sph = struct.unpack_from("<4h", b, o + 8)
            groups.append(dict(off=o, nslots=ns, type=gh[1] & 0xffff, sphere=sph))
            o += ns * 16
        objs.append(dict(off=off, index=h[1] & 0xffff, matrix=mat, groups=groups))
        off = o
    if off != len(b):
        raise ValueError("container does not end at EOF: %#x != %#x" % (off, len(b)))
    return objs


def group_items(b, g):
    """Return the 8-byte s16x4 items after the header slot."""
    n = (g["nslots"] - 1) * 2
    return [struct.unpack_from("<4h", b, g["off"] + 16 + 8 * i) for i in range(n)]


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    for o in parse(data):
        print("obj", o["index"], hex(o["off"]), "origin", [round(x, 1) for x in o["matrix"][12:15]],
              "groups", len(o["groups"]))
        for g in o["groups"][:8]:
            print("   ", g)
