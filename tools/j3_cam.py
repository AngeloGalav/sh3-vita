#!/usr/bin/env python3
"""SH3 .cam parser (J3). Layout from the exe, static analysis 2026-11-12:
  fix-up 0x5a1350 (on load) and zone builder 0x4c0d10 (camera module).

A .cam file is an array of 0x80-byte records. A record whose byte at +0x44
has bit 0 set is the terminator (the exe tests it before processing; the
trailing ASCII junk in some files lands here). Fields (offsets in bytes):
  +0x00  p0 (x,z)   +0x08 p1 (x,z)   +0x10 p2 (x,z)      float x6   zone A
  +0x18  y0   +0x1c y1                                     float x2   height range (min/max y)
  +0x20  q0 (x,z)   +0x28 q1 (x,z)   +0x30 q2 (x,z)      float x6   zone B
  +0x38,+0x3c  float x2 (copied to entry +0x28/+0x2c)
  +0x40  s16 (widened to s32 by the fix-up)
  +0x44  u32 flags: bit0 = terminator, bit7 = passed to the distance helper 0x4c02d0
  +0x48,+0x4c,+0x50  u32 ids/types
  +0x54  float cam param 0: 10001/10002/10003 -> -300/-375/-600
  +0x58  float cam param 1: 10001/10002/10003 -> -300/-150/+150
  +0x5c  u32 (0/1/2)
  +0x60  float cam param 2: 0 -> 448, 10002 -> 384
  +0x6c  u32 selector: 0 -> +0x70 sentinel 10001/10002 -> -500/-125;
                       2 -> +0x70 sentinel 10001 -> -0.5236 rad (-30 deg)
  +0x70  float (see selector)
  +0x74,+0x78,+0x7c floats (limits)
Zones A/B: each is an oriented rectangle: edge p0->p1 gives the yaw (atan2 of
dx,dz), p2 the extent; the zone builder rotates the points by that yaw and keeps
the min/max box (entry +0x18..+0x3c), plus the 4x4 rotation matrices copied to
entry +0x48/+0x88/+0xc8/+0x108 (entry size 0x148, array at ctx+0x1d4).
Usage: j3_cam.py FILE.cam            dump records
       j3_cam.py --survey FILE...    one line per file
"""
import struct
import sys

SENT = {10001.0: 1, 10002.0: 2, 10003.0: 3}
FIX = {  # offset -> {sentinel: replacement}
    0x54: {10001.0: -300.0, 10002.0: -375.0, 10003.0: -600.0},
    0x58: {10001.0: -300.0, 10002.0: -150.0, 10003.0: 150.0},
}


def records(b):
    out = []
    n = len(b) // 0x80
    for i in range(n):
        r = b[i * 0x80:(i + 1) * 0x80]
        flags = struct.unpack_from("<I", r, 0x44)[0]
        if flags & 1:
            break
        f = struct.unpack_from("<32f", r, 0)
        d = dict(
            idx=i,
            zoneA=[f[0:2], f[2:4], f[4:6]],
            y=(f[6], f[7]),
            zoneB=[f[8:10], f[10:12], f[12:14]],
            f38=(f[14], f[15]),
            s16=struct.unpack_from("<h", r, 0x40)[0],
            flags=flags,
            ids=struct.unpack_from("<3I", r, 0x48),
            cam0=f[0x54 // 4], cam1=f[0x58 // 4], k5c=struct.unpack_from("<I", r, 0x5c)[0],
            cam2=f[0x60 // 4], sel=struct.unpack_from("<I", r, 0x6c)[0], f70=f[0x70 // 4],
            lim=(f[0x74 // 4], f[0x78 // 4], f[0x7c // 4]),
        )
        for off, tab in FIX.items():
            v = f[off // 4]
            d["cam%d" % ((off - 0x54) // 4)] = tab.get(v, v)
        if d["cam2"] == 0.0:
            d["cam2"] = 448.0
        elif d["cam2"] == 10002.0:
            d["cam2"] = 384.0
        out.append(d)
    return out


def main(a):
    if len(a) >= 3 and a[1] == "--survey":
        tot = 0
        for p in a[2:]:
            rs = records(open(p, "rb").read())
            tot += len(rs)
            print("%s %d" % (p.rsplit("/", 1)[-1], len(rs)))
        print("total", tot)
        return 0
    for r in records(open(a[1], "rb").read()):
        print(r)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
