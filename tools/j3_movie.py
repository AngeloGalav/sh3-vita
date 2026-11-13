#!/usr/bin/env python3
"""SH3 .000 movie decryptor (J3 Tier 3, solved 2026-11-13).

The seven data/movie/*.000 files (op, ed_b, ed_u, ed_n, henshin, huxx, muxx)
are MPEG-2 program streams (2048-byte packs, 00 00 01 BA ... 00 00 01 B9)
obfuscated with an MSVC-CRT LCG keystream. Routine located statically in
sh3.exe FUN_00405680 (movie DirectShow source-filter read, ReadFile site
0x4056b2); verified end-to-end on all 7 files: every 2048-byte sector
decrypts to a pack sync, file tail ends with the pack end code.

Scheme:
  * The file starts with a 0x8000-byte block whose plaintext is all zeros
    (the exe scan skips leading zero words). Its ciphertext carries the
    seed: 4 bytes at fixed offsets, gathered as
        seed = f[0x2dde] | f[0x3d40]<<8 | f[0x2a5c]<<16 | f[0x55e2]<<24
  * LCG: state = state * 0x5d588b65 + 1  (mod 2^32; the MSVC rand() mult).
  * base = LCG^8192(seed)   (i.e. the state after running over the padding).
  * Data region (from 0x8000): the keystream is PERIODIC with period 0x1000
    bytes: at every 4096-byte chunk the state resets to `base`, then per
    32-bit word: state = step(state); plain_word = cipher_word ^ state.
  * FUN_00405680 also zeroes the 4 seed bytes in its decrypted block; that
    only concerns the padding block, the data region is untouched.

Usage: j3_movie.py IN.000 [OUT.mpg]     (stdout if OUT omitted)
       j3_movie.py --check IN.000        verify pack syncs, no output
"""
import struct
import sys

M = 0x5D588B65
MASK = 0xFFFFFFFF


def base_state(header8000):
    seed = (header8000[0x2DDE] | (header8000[0x3D40] << 8) |
            (header8000[0x2A5C] << 16) | (header8000[0x55E2] << 24))
    st = seed
    for _ in range(0x8000 // 4):
        st = (st * M + 1) & MASK
    return st


def decrypt_chunk(buf, st_base):
    out = bytearray(buf)
    n = len(out) - len(out) % 4
    st = st_base
    for i in range(0, n, 4):
        st = (st * M + 1) & MASK
        struct.pack_into("<I", out, i, struct.unpack_from("<I", out, i)[0] ^ st)
    return bytes(out), st


def check(path):
    with open(path, "rb") as f:
        hdr = f.read(0x8000)
        if len(hdr) < 0x8000:
            print("file smaller than padding block")
            return 1
        base = base_state(hdr)
        seed = (hdr[0x2DDE] | (hdr[0x3D40] << 8) |
                (hdr[0x2A5C] << 16) | (hdr[0x55E2] << 24))
        pad, _ = decrypt_chunk(hdr, seed)
        pad = bytearray(pad)
        # the 4 seed bytes decrypt to themselves; FUN_00405680 zeroes them
        for o in (0x2DDE, 0x3D40, 0x2A5C, 0x55E2):
            pad[o] = 0
        if any(pad):
            print("WARNING: padding block does not decrypt to all zeros")
        bad = tot = off = 0
        while True:
            chunk = f.read(0x1000)
            if not chunk:
                break
            dec, _ = decrypt_chunk(chunk, base)
            for s in range(0, len(dec) - 2047, 2048):  # full sectors only
                tot += 1
                if dec[s:s + 4] != b"\x00\x00\x01\xba":
                    bad += 1
                    print("bad sector at %#x" % (0x8000 + off + s))
            off += len(chunk)
        print("%s: %d sectors, %d bad" % (path, tot, bad))
        return 1 if bad else 0


def decrypt(path, out_path):
    with open(path, "rb") as f:
        base = base_state(f.read(0x8000))
        with open(out_path, "wb") as g:
            while True:
                chunk = f.read(0x1000)
                if not chunk:
                    break
                dec, _ = decrypt_chunk(chunk, base)
                g.write(dec)


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--check":
        sys.exit(check(a[1]))
    if len(a) < 2:
        print(__doc__)
        sys.exit(2)
    decrypt(a[0], a[1])
