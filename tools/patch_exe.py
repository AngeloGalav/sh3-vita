#!/usr/bin/env python3
"""Byte patches for sh3.exe, applied to a COPY before recompilation: the widescreen fix, and the
port's own title-menu change (no Exit row: a Vita game is closed from the system).

Port of ThirteenAG's SilentHill3.WidescreenFix (GPL-3.0, github.com/ThirteenAG/WidescreenFixesPack)
to the static recompiler, following the approach of sh3-nx (cutarev/sh3-nx): the parts of the fix
that change code in the middle of a function become byte patches here; the parts that replace whole
functions are hand-written natives (port/src/host/sh3_widescreen.c, wired in recomp/x86rec.py).
Every site is checked against the original bytes of the 2003 exe (SHA-256
8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918).

The port renders at a fixed wide size (960x544 on the Vita, the host window size otherwise), so
the cinematic side bars and the cutscene borders are patched to the exe's own 0.0f constant
(0x68A868) instead of sh3-nx's runtime variable: on a fixed wide screen they are always off.

Usage:
  patch_exe.py <in-sh3.exe> <out-sh3.exe> [functions-sh3.json ...]
When one or more functions JSON files are given, the health-indicator cave function (0x401ED8,
nop padding after a ret in the original) is added to each so the recompiler emits F_00401ed8
for the redirected call at 0x5F1D15 (idempotent).
"""
import json
import struct
import sys

BASE = 0x400000
ZERO = 0x68A868          # the exe's own 0.0f float constant (.rdata)
BARS_SIDE = 0x68E6D4     # 1/512: the 2-pixel black bars at the sides of the screen
BORDER_HALF = 0x68A8F0   # 0.5: the cinematic borders' half height
CAVE_HEALTH = 0x401ED8   # nop padding after a ret; x86rec.py OVERRIDES it with nat_ws_health


def p32(v):
    return struct.pack('<I', v & 0xFFFFFFFF)


def call(at, target):
    return b'\xE8' + struct.pack('<i', target - (at + 5))


# (va, original bytes, replacement, what)
PATCHES = [
    (0x41BB2D, p32(BARS_SIDE), p32(ZERO), 'black bars at the sides of the screen: off'),
    (0x41BE66, p32(BARS_SIDE), p32(ZERO), 'black bars at the sides of the screen: off'),
    (0x41BB4D, p32(BORDER_HALF), p32(ZERO), 'cutscene borders: off on the fixed wide screen'),
    (0x5DAFE6, p32(0x100), p32(0x400), 'inventory black backdrop: half width 256 -> 1024'),
    (0x5DAFEB, p32(-0x100), p32(-0x400), 'inventory black backdrop: half width 256 -> 1024'),
    (CAVE_HEALTH, b'\x90', b'\xC3', 'health-indicator cave: nop -> ret (the native replaces it)'),
    (0x5F1D15, bytes.fromhex('d959e8d9c0'), call(0x5F1D15, CAVE_HEALTH),
     'low-health cross: fstp [ecx-0x18]; fld st(0) -> call the cave (keeps its place in the 4:3 area)'),
    (0x5E82FD, b'\x6a\x00', b'\x6a\x01', 'pause menu box: alpha blending on (sh3-nx/FusionFix)'),
    (0x5E8390, p32(0xFF000000), p32(0x00000000), 'pause menu box: fully transparent (sh3-nx/FusionFix)'),
    # Not from the widescreen fix: the port's own changes to the title menu.
    (0x5D812A, bytes.fromhex('c744ac180900000045'), b'\x90' * 9,
     'title menu: no Exit row (FUN_005d80c0 appends row id 9 last; the rows lay themselves out from their count)'),
]

CAVE_FUNC = {
    "a": "0x401ed8",
    "name": "FUN_00401ed8",
    "thunk": False,
    "target": None,
    "insns": [CAVE_HEALTH],
}


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    d = bytearray(open(sys.argv[1], 'rb').read())
    for va, old, new, what in PATCHES:
        o = va - BASE  # .text file offsets equal RVAs in this exe
        if d[o:o + len(old)] != old:
            sys.exit(f'{va:#x} ({what}): expected {old.hex()}, found {d[o:o + len(old)].hex()}')
        d[o:o + len(new)] = new
    open(sys.argv[2], 'wb').write(d)
    print(f'{len(PATCHES)} patches -> {sys.argv[2]}')

    for path in sys.argv[3:]:
        funcs = json.load(open(path))
        if any(f.get('a') == CAVE_FUNC['a'] for f in funcs):
            print(f'{path}: cave function already present')
            continue
        funcs.append(CAVE_FUNC)
        funcs.sort(key=lambda f: int(f['a'], 16))
        json.dump(funcs, open(path, 'w'))
        print(f'{path}: added cave function {CAVE_FUNC["a"]}')


if __name__ == '__main__':
    main()
