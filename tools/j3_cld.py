#!/usr/bin/env python3
"""SH3 .cld collision files (decoded 2026-11-11; validated on all 288 files).

Layout (all little endian):
  +0x00 float x0, float z0        world origin of a 4x4 grid of 5000-unit cells (covers 20000x20000;
                                  exe: cell size const 0x690028 = 5000.0, extent 0x68f130 = 20000.0)
  +0x08 u32 size of section 0..4  (bytes; polygon sections 0..3, cylinder section 4)
  +0x1c u32 0
  +0x20 u32 table[85]             table[0..79] = list pointers, cat-major: table[cat*16 + cell], cat 0..3 and
                                  cell = zi*4 + xi (zi from z0, xi from x0; exe loop 0x4afbb0);
                                  table[80..84] = absolute offsets of sections 0..4
  lists: u32 polygon indices terminated by 0xffffffff (index into the section's record array; record 0 of
         each polygon section is an empty record type 0 / nverts 0)
  polygon record (0x50 bytes): u32 type (0x101 or 1), u32 nverts (4; 0 = empty), u32 flags, u32 0,
         then 4 x (x, y, z, 1.0) world-space vertices
  cylinder record (0x30 bytes, section 4): 16 bytes zero, (x, y, z, 1.0) base position, (0, -height, 0, radius)
Sections 1 and 3 are almost entirely vertical quads (walls; section 3 = low ledges, flag 0x1000);
sections 0 and 2 mix floors/ceilings/slopes/walls. Flag bits seen: 0x1, 0x2, 0x4, 0x20, 0x40, 0x80, 0x100,
0x200, 0x800, 0x1000, 0x4000, 0x8000, 0x20000 (meaning not decoded).
"""
import struct
import sys


def parse(d):
    x0, z0 = struct.unpack_from('<2f', d, 0)
    sizes = struct.unpack_from('<5I', d, 8)
    t0 = struct.unpack_from('<I', d, 0x20)[0]
    ntab = (t0 - 0x20) // 4
    tab = struct.unpack_from('<%dI' % ntab, d, 0x20)
    assert ntab == 85, ntab
    secs = tab[80:85]
    polys = []
    for k in range(4):
        recs = []
        for o in range(secs[k], secs[k] + sizes[k], 0x50):
            t, nv, fl, _ = struct.unpack_from('<4I', d, o)
            verts = [struct.unpack_from('<3f', d, o + 0x10 + 16 * j) for j in range(nv)]
            recs.append(dict(type=t, flags=fl, verts=verts))
        polys.append(recs)
    cyls = []
    for o in range(secs[4], secs[4] + sizes[4], 0x30):
        f = struct.unpack_from('<12f', d, o)
        if f[7] != 0:
            cyls.append(dict(pos=f[4:7], height=-f[9], radius=f[11]))
    cells = {}
    for cat in range(4):
        for cell in range(16):
            o = tab[cat * 16 + cell]
            lst = []
            while True:
                v = struct.unpack_from('<I', d, o)[0]
                if v == 0xFFFFFFFF:
                    break
                lst.append(v)
                o += 4
            cells[(cat, cell)] = lst
    return dict(origin=(x0, z0), polys=polys, cylinders=cyls, cells=cells)


if __name__ == '__main__':
    sys.path.insert(0, __file__.rsplit('/', 1)[0])
    import j3_arc
    A, _ = j3_arc.load_all(sys.argv[1])
    for a in A.values():
        for i, e in enumerate(a.entries):
            if e.path.endswith(sys.argv[2]):
                r = parse(a.payload(i))
                print(e.path, r['origin'], [len(p) for p in r['polys']], 'cylinders', len(r['cylinders']))
