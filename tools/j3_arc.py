#!/usr/bin/env python3
"""J3: .arc / arc.arc reader for the SH3 preparer.

Format reverse-engineered 2026-11-11, validated against all 30 game archives.

<name>.arc (unencrypted):
  header (16 B):  magic 07 05 03 20 | u32 count | u32 aux_size | u32 0
  records (16 B x count): u32 offset | u32 aux_ptr | u32 size | u32 size2 (== size)
  aux table (aux_size B): all zeros in shipped files (runtime scratch). Each
      record's aux_ptr points into this region; stride is variable and blocks
      may be shared between entries.
  payloads: concatenated; offset[n]+size[n]==offset[n+1]; last end == file size.

arc.arc (gzip-compressed path table):
  header (16 B): magic 17 04 03 20 + zeros
  records: u16 type, u16 reclen (incl. those 4 B), then
    type 1: u32 n_archives, u32 n_paths
    type 2: u32 entry_count, name\0        (archive stem; file is <name>.arc)
    type 3: u32 index = (archive ordinal << 16) | entry, path\0 (reclen padded to even)

ffffffff container (.tex room/stills, .tbn2, .map, .sbn payload):
  0x00: u32 ffffffff | u32 0 | u32 hdr_size (0x20) | u32 total
        | u32 0 | u32 sub_count | u32 0 | u32 0
  sub-blocks start at 0x20, each:
    +0x00 u32 ffffffff | u32 0
    +0x08 u16 w | u16 h | u8 fmt | u8 tag (0x30/0x50/0x20) | u16 0
    +0x10 u32 pixel_size | u32 stride_to_next_sub | u32 0 | u32 marker 0x99990909
    pixels follow the header: px = sub_off + stride - pixel_size (header is
    0x60 or 0x80 bytes depending on stride; the stride field is authoritative).
  fmt 0x20 = 32-bit BGRA linear (room tex render correctly with a B<->R swap;
  same fmt value means BGRA in the SH2 converter, sh2conv/ui.py).
  fmt 0x08 = 8-bit indexed, pixel_size == w*h; a palette block (u32 0x1000 +
  256 BGRX entries) follows the pixels -- exact index->color mapping unresolved,
  see docs/DATA-FORMATS-SH3.md.
  data/tmp/<room>GB.tex = room-family atlas: 7 subs, 6x512x512 + 1x512x1024,
  all fmt 0x20; verified to decode as color texture atlas pages.

Usage:
  j3_arc.py validate <data dir>
  j3_arc.py ls <data dir> <archive>
  j3_arc.py extract <data dir> <archive> <idx> -o OUT [--path PATH]
  j3_arc.py sniff <data dir> [archive ...]
  j3_arc.py tex <data dir> <archive> -o OUT [--path PATH]
"""
import argparse
import gzip
import os
import struct
import sys
from collections import Counter

ARC_MAGIC = bytes.fromhex("07050320")
TABLE_MAGIC = bytes.fromhex("17040320")


class Entry:
    __slots__ = ("offset", "aux_ptr", "size", "path")

    def __init__(self, offset, aux_ptr, size):
        self.offset, self.aux_ptr, self.size = offset, aux_ptr, size
        self.path = None


class Archive:
    def __init__(self, name, path):
        self.name = name
        self.path = path
        with open(path, "rb") as f:
            hdr = f.read(16)
        if hdr[:4] != ARC_MAGIC:
            raise ValueError(f"{path}: bad magic {hdr[:4].hex()}")
        _, self.count, self.aux_size, z = struct.unpack("<4I", hdr)
        self.entries = []
        with open(path, "rb") as f:
            f.seek(16)
            for _ in range(self.count):
                off, aux, size, size2 = struct.unpack("<4I", f.read(16))
                assert size == size2, f"{path}: size fields differ"
                self.entries.append(Entry(off, aux, size))

    def payload(self, idx):
        e = self.entries[idx]
        with open(self.path, "rb") as f:
            f.seek(e.offset)
            return f.read(e.size)

    def find(self, path):
        for i, e in enumerate(self.entries):
            if e.path and e.path.lower() == path.lower():
                return i
        raise KeyError(path)


def read_table(data_dir):
    """Parse arc.arc -> {archive name: [path per entry index]} plus stats."""
    with gzip.open(os.path.join(data_dir, "arc.arc"), "rb") as f:
        raw = f.read()
    if raw[:4] != TABLE_MAGIC:
        raise ValueError(f"arc.arc: bad magic {raw[:4].hex()}")
    groups, order = {}, []
    stats = None
    off, cur = 16, None
    while off < len(raw):
        typ, reclen = struct.unpack_from("<HH", raw, off)
        if reclen == 0 or off + reclen > len(raw):
            raise ValueError(f"arc.arc: bad record at {off}")
        body = raw[off + 4:off + reclen]
        if typ == 1:
            stats = struct.unpack("<2I", body)
        elif typ == 2:
            cur = body[4:].rstrip(b"\0").decode("latin1")
            groups[cur] = [None] * struct.unpack("<I", body[:4])[0]
            order.append(cur)
        elif typ == 3:
            idx = struct.unpack("<I", body[:4])[0]
            p = body[4:].rstrip(b"\0").decode("latin1")
            groups[cur][idx & 0xFFFF] = p
        else:
            raise ValueError(f"arc.arc: unknown record type {typ} at {off}")
        off += reclen
    return groups, order, stats


def load_all(data_dir):
    groups, order, stats = read_table(data_dir)
    archives = {}
    for name in order:
        a = Archive(name, os.path.join(data_dir, name + ".arc"))
        assert a.count == len(groups[name]), f"{name}: count {a.count} != table {len(groups[name])}"
        for i, p in enumerate(groups[name]):
            a.entries[i].path = p
        archives[name] = a
    return archives, order


def cmd_validate(args):
    archives, order = load_all(args.data_dir)
    total_entries = total_bytes = 0
    ok = True
    for name in order:
        a = archives[name]
        fsize = os.path.getsize(a.path)
        problems = []
        if a.entries[0].offset != 16 + 16 * a.count + a.aux_size:
            problems.append(f"first offset {a.entries[0].offset} != table end")
        with open(a.path, "rb") as f:
            f.seek(16 + 16 * a.count)
            aux = f.read(a.aux_size)
        if aux.strip(b"\0"):
            problems.append("aux table not all zeros")
        for i, e in enumerate(a.entries):
            if not (16 + 16 * a.count <= e.aux_ptr < a.entries[0].offset):
                problems.append(f"entry {i}: aux_ptr {e.aux_ptr} outside aux region")
            if i + 1 < len(a.entries) and e.offset + e.size != a.entries[i + 1].offset:
                problems.append(f"entry {i}: gap/overlap with next")
            if any(p is None for p in (e.path,)):
                problems.append(f"entry {i}: no path in arc.arc")
        last = a.entries[-1]
        if last.offset + last.size != fsize:
            problems.append(f"last entry ends {last.offset + last.size} != file size {fsize}")
        total_entries += a.count
        total_bytes += fsize
        status = "OK" if not problems else "FAIL"
        if problems:
            ok = False
        print(f"{name:8s} {a.count:4d} entries  {fsize:11,d} B  {status}")
        for p in problems[:5]:
            print(f"    {p}")
    print(f"\n{len(order)} archives, {total_entries} entries, {total_bytes:,} B -- "
          f"{'ALL OK' if ok else 'PROBLEMS'}")
    return 0 if ok else 1


def cmd_ls(args):
    archives, _ = load_all(args.data_dir)
    a = archives[args.archive]
    for i, e in enumerate(a.entries):
        print(f"{i:4d}  {e.offset:11,d}  {e.size:10,d}  {e.path}")
    return 0


def cmd_extract(args):
    archives, _ = load_all(args.data_dir)
    a = archives[args.archive]
    idx = a.find(args.path) if args.path else int(args.idx)
    data = a.payload(idx)
    os.makedirs(args.out, exist_ok=True)
    dest = os.path.join(args.out, os.path.basename(a.entries[idx].path or f"{args.archive}_{idx}"))
    with open(dest, "wb") as f:
        f.write(data)
    print(f"{args.archive}[{idx}] -> {dest} ({len(data):,} B, sha256-payload)")
    return 0


MAGICS = [
    (b"BM", "bmp"), (b"\x89PNG", "png"), (b"\xff\xd8\xff", "jpeg"),
    (b"OggS", "ogg"), (b"RIFF", "riff/wav"), (b"\x80\x00", "adx?"),
    (b"ID3", "mp3"), (b"PK\x03\x04", "zip"), (b"\x1f\x8b", "gzip"),
    (b"ZLIB", "zlib"), (b"PS", "psd?"),
]


def cmd_sniff(args):
    archives, order = load_all(args.data_dir)
    names = args.archive or order
    for name in names:
        a = archives[name]
        kinds = Counter()
        exts = Counter()
        for i, e in enumerate(a.entries):
            head = a.payload(i)[:8]
            kind = next((k for m, k in MAGICS if head.startswith(m)), None)
            if kind is None:
                kind = f"raw:{head[:4].hex()}"
            kinds[kind] += 1
            p = e.path or ""
            exts[os.path.splitext(p)[1].lower() or "(none)"] += 1
        print(f"{name:8s} {a.count:4d}  {dict(kinds.most_common(6))}")
        print(f"         ext {dict(exts.most_common(8))}")
    return 0


def _png_save(path, w, h, rgb):
    """Minimal RGB PNG writer (no deps): `rgb` is w*h*3 bytes, row-major."""
    import zlib
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def walk_subs(d):
    """ffffffff container -> list of sub-block dicts. The u32 at sub+0x14 is the
    stride to the next sub-header (header size = stride - pixel_size, 0x60 or
    0x80 in observed files); it is authoritative, do not assume a fixed stride."""
    if d[:4] != b"\xff\xff\xff\xff":
        raise ValueError("not a ffffffff container")
    count = struct.unpack_from("<I", d, 0x14)[0]
    off, subs = 0x20, []
    for i in range(count):
        if off + 0x20 > len(d):
            raise ValueError(f"sub {i} @0x{off:x}: past end of file")
        magic, = struct.unpack_from("<I", d, off)
        if magic != 0xFFFFFFFF:
            raise ValueError(f"sub {i} @0x{off:x}: bad magic {magic:08x}")
        w, h, fmt, tag = struct.unpack_from("<HHBB", d, off + 8)
        size, stride, _z, marker = struct.unpack_from("<4I", d, off + 0x10)
        subs.append(dict(off=off, w=w, h=h, fmt=fmt, tag=tag, size=size,
                         stride=stride, marker=marker, px=off + stride - size))
        off += stride
    return subs


def bgra_to_rgb(px):
    rgb = bytearray(len(px) // 4 * 3)
    rgb[0::3] = px[2::4]  # R
    rgb[1::3] = px[1::4]  # G
    rgb[2::3] = px[0::4]  # B
    return bytes(rgb)


def decode_container(d):
    """ffffffff container -> list of (w, h, fmt, rgb_bytes) for every fmt 0x20
    sub-block; raises ValueError for containers with no decodable sub."""
    subs = walk_subs(d)
    out = []
    for s in subs:
        if s["fmt"] != 0x20:
            continue
        if s["px"] + s["size"] > len(d):
            raise ValueError("truncated: pixels past end of file")
        out.append((s["w"], s["h"], s["fmt"], bgra_to_rgb(d[s["px"]:s["px"] + s["size"]])))
    if not out:
        raise ValueError("no fmt 0x20 sub-block (see docs/DATA-FORMATS-SH3.md)")
    return out


def cmd_tex(args):
    archives, _ = load_all(args.data_dir)
    a = archives[args.archive]
    os.makedirs(args.out, exist_ok=True)
    idxs = [a.find(args.path)] if args.path else range(a.count)
    done = skipped = 0
    for i in idxs:
        d = a.payload(i)
        name = os.path.splitext(os.path.basename(a.entries[i].path or str(i)))[0]
        try:
            layers = decode_container(d)
        except ValueError as exc:
            print(f"  {name}: {exc} -- skipped")
            skipped += 1
            continue
        for j, (w, h, fmt, rgb) in enumerate(layers):
            dest = os.path.join(args.out, f"{a.name}_{name}_{j}.png")
            _png_save(dest, w, h, rgb)
            print(f"  {name}[{j}]: {w}x{h} BGRA32 -> {dest}")
            done += 1
    print(f"{done} decoded, {skipped} skipped")
    return 0 if done else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["validate", "ls", "extract", "sniff", "tex"])
    ap.add_argument("data_dir")
    ap.add_argument("archive", nargs="?")
    ap.add_argument("idx", nargs="?")
    ap.add_argument("-o", "--out", default=".")
    ap.add_argument("--path")
    args = ap.parse_args()
    return {"validate": cmd_validate, "ls": cmd_ls, "extract": cmd_extract,
            "sniff": cmd_sniff, "tex": cmd_tex}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
