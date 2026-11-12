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
  0x00: u32 ffffffff | u32 0 | u32 hdr_size (0x20) | u32 total (offset of the
        last sub-header) | u32 0 | u32 sub_count (unreliable, 0 for some pic
        files -- walk subs by magic) | u32 0 | u32 0
  sub-blocks start at 0x20, each:
    +0x00 u32 ffffffff | u32 0
    +0x08 u16 w | u16 h | u8 fmt | u8 tag (0x30/0x50/0x20) | u16 0
    +0x10 u32 pixel_size | u32 stride_to_next_sub | u32 0 | u32 marker 0x99990909
    pixels follow the header: px = sub_off + stride - pixel_size (header is
    0x60 or 0x80 bytes depending on stride; the stride field is authoritative).
  fmt 0x20 / 0x18 = 32-bit BGRA linear, pixel_size == w*h*4 (room tex and pic
  stills render correctly with a B<->R swap; same fmt value means BGRA in the
  SH2 converter, sh2conv/ui.py).
  fmt 0x10 = 16-bit RGB555, pixel_size == w*h*2, bit15 set on every pixel
  (A1R5G5B5, R = top 5 bits). Verified on ccGB.tex pages (STOP sign red,
  green-screen green) and on room .map inner pages.
  fmt 0x04 = 4bpp (pixel_size == w*h/2), NOT plain DXT1 (linear DXT1 decode is
  noise); 3 subs in 3 pic files, parked.

.map payload (SOLVED 2026-11-11): outer ffffffff container, header 0x50 B:
  u32 ffffffff | 0 | 0 | 0x50 | u32 inner_off | 0 | 0x50 | u32 +0x1C (0/0x130/...)
  Geometry blob 0x50..inner_off = section tree; inner_off holds a standard
  ffffffff container (sub_count reliable) with the room's own texture pages
  (fmt 0x20 BGRA, sometimes 0x10 16bpp); last sub ends exactly at EOF.
  Section header (0x20/0x30 B): u32 next | u32 hdr_size | u32 size (incl. hdr) |
  0 | u32 type | ... Top-level sections are contiguous; children inside a
  wrapper are linked by `next` (absolute offset, 0 = end). Wrapper types seen:
  0,1,2,3,4,8,9,10,256,265,0x10000..0x1000f. Type-1 section = 4x4 matrix +
  bbox corner floats. Leaf sections (hdr 0x40): type == vertex count, payload
  = triangle list of 0x24-byte records:
    float3 pos | float3 normal | float2 uv | u32 rgba (bytes R,G,B,A)
  (33,261/33,271 children match across all 287 maps; degenerate filler
  triangles present). Child body at +0x18 = texture/material id (0..~67,
  grouped in blocks of 32), +0x1C = 0x3c constant.
  fmt 0x08 = 8-bit indexed, pixel_size == w*h. A palette block follows the
  pixels and is NOT covered by the stride field: u32 0x1000 (byte size) + 1024
  RGBX entries + 0x2C zero trailer (0x1030 B total). The index byte splits as
  row = i >> 4, color = i & 15; the 16 colors of row r sit at entries
  r*64 + 11 .. r*64 + 26 (empirically, 455/455 rows in shipping files).
  color = pal[(i >> 4) * 64 + 11 + (i & 15)], R = first byte. See
  docs/DATA-FORMATS-SH3.md for the evidence.
  data/tmp/<room>GB.tex = room-family atlas: 7 subs, 6x512x512 + 1x512x1024,
  all fmt 0x20; verified to decode as color texture atlas pages.

Usage:
  j3_arc.py validate <data dir>
  j3_arc.py ls <data dir> <archive>
  j3_arc.py extract <data dir> <archive> <idx> -o OUT [--path PATH]
  j3_arc.py sniff <data dir> [archive ...]
  j3_arc.py tex <data dir> <archive> -o OUT [--path PATH]
  j3_arc.py map <data dir> <archive> -o OUT [--path PATH]   (.map tree + pages)
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
    """RGB PNG writer: `rgb` must be exactly w*h*3 bytes, row-major."""
    if len(rgb) != w * h * 3:
        raise ValueError(f"_png_save: got {len(rgb)} B, expected {w*h*3} "
                         f"({w}x{h} RGB). Convert indexed/BGRA data first.")
    from PIL import Image
    Image.frombytes("RGB", (w, h), bytes(rgb)).save(path)


def walk_subs(d):
    """ffffffff container -> list of sub-block dicts. The u32 at sub+0x14 is the
    stride to the next sub-header (header size = stride - pixel_size, 0x50/0x60/
    0x80 in observed files); it is authoritative, do not assume a fixed stride.
    For fmt 0x08 the stride covers header+pixels only: a 0x1030-byte palette
    block (u32 0x1000 + 1024 RGBX entries + 0x2C zeros) follows the pixels and
    is skipped here; its offset is recorded as s["pal"]."""
    base = 0
    if d[:4] != b"\xff\xff\xff\xff":
        # pic .tex files carry a 0x40-byte zero preamble before the container
        if d[0x40:0x44] == b"\xff\xff\xff\xff":
            base = 0x40
        else:
            raise ValueError("not a ffffffff container")
    # The u32 sub_count at base+0x14 is unreliable (0 for some single-sub pic
    # files), so walk by magic instead: subs follow each other at `stride`, and
    # the walk ends at the first non-ffffffff dword (or end of file).
    off, subs = base + 0x20, []
    while off + 0x20 <= len(d) and d[off:off + 4] == b"\xff\xff\xff\xff":
        w, h, fmt, tag = struct.unpack_from("<HHBB", d, off + 8)
        size, stride, _z, marker = struct.unpack_from("<4I", d, off + 0x10)
        s = dict(off=off, w=w, h=h, fmt=fmt, tag=tag, size=size,
                 stride=stride, marker=marker, px=off + stride - size)
        off += stride
        if fmt == 0x08 and off + 4 <= len(d) and struct.unpack_from("<I", d, off)[0] == 0x1000:
            s["pal"] = off
            off += 0x1030
        subs.append(s)
    if not subs:
        raise ValueError("no sub-block found at 0x%x" % (base + 0x20))
    return subs


def bgra_to_rgb(px):
    rgb = bytearray(len(px) // 4 * 3)
    rgb[0::3] = px[2::4]  # R
    rgb[1::3] = px[1::4]  # G
    rgb[2::3] = px[0::4]  # B
    return bytes(rgb)


def pal8_lut(pal_bytes):
    """fmt 0x08 palette block (1024 RGBX entries) -> 768-byte RGB LUT indexed by
    the pixel byte. PS2 CLUT swizzle (exe table 0x6bd840, converter 0x4922d0):
    t = (i & 0xe7) | ((i & 8) << 1) | ((i & 0x10) >> 1);
    color = pal[(t >> 4) * 64 + 11 + (t & 15)], R = first byte. (Alpha byte is
    PS2-range 0..0x80; the exe uses min(255, 2*a).)"""
    lut = bytearray(256 * 3)
    for i in range(256):
        t = (i & 0xE7) | ((i & 8) << 1) | ((i & 0x10) >> 1)
        v, = struct.unpack_from("<I", pal_bytes, 4 * ((t >> 4) * 64 + 11 + (t & 15)))
        lut[i * 3], lut[i * 3 + 1], lut[i * 3 + 2] = v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF
    return bytes(lut)


def unswizzle8(buf, w, h):
    """PS2 GS PSMT8 (8bpp, 32-bit page layout) -> linear indices. fmt 0x08 pixel
    data is stored in this swizzled order (data-derived: 263/314 images get
    smoother, mean neighbour roughness 34.8 -> 15.7; fire00 becomes a smooth
    flame). Needs w, h multiples of 16."""
    if w % 16 or h % 16 or len(buf) < w * h:
        return bytes(buf[:w * h])
    out = bytearray(w * h)
    for y in range(h):
        row = y * w
        by = (y & ~0xF) * w
        ss = (((y + 2) >> 2) & 1) * 4
        py = ((((y & ~3) >> 1) + (y & 1)) & 7) * w * 2
        b2 = (y >> 1) & 1
        for x in range(w):
            idx = by + (x & ~0xF) * 2 + py + ((x + ss) & 7) * 4 + b2 + ((x >> 2) & 2)
            out[row + x] = buf[idx]
    return bytes(out)


def pal8_to_rgb(pix, lut):
    out = bytearray(len(pix) * 3)
    for k, i in enumerate(pix):
        j = i * 3
        out[k * 3], out[k * 3 + 1], out[k * 3 + 2] = lut[j], lut[j + 1], lut[j + 2]
    return bytes(out)


def rgb555_to_rgb(px):
    """fmt 0x10: little-endian A1R5G5B5 (bit15 set) -> RGB bytes."""
    out = bytearray(len(px) // 2 * 3)
    for k in range(len(px) // 2):
        v = px[k * 2] | (px[k * 2 + 1] << 8)
        r, g, b = (v >> 10) & 0x1F, (v >> 5) & 0x1F, v & 0x1F
        out[k * 3:k * 3 + 3] = bytes([(r << 3) | (r >> 2), (g << 3) | (g >> 2),
                                       (b << 3) | (b >> 2)])
    return bytes(out)


def decode_container(d):
    """ffffffff container -> list of (w, h, fmt, rgb_bytes) for every decodable
    sub-block (fmt 0x20/0x18 BGRA, fmt 0x08 paletted); raises ValueError for
    containers with no decodable sub."""
    subs = walk_subs(d)
    out = []
    for s in subs:
        if s["px"] + s["size"] > len(d):
            raise ValueError("truncated: pixels past end of file")
        if s["fmt"] in (0x20, 0x18):
            out.append((s["w"], s["h"], s["fmt"], bgra_to_rgb(d[s["px"]:s["px"] + s["size"]])))
        elif s["fmt"] == 0x10 and s["size"] == s["w"] * s["h"] * 2:
            out.append((s["w"], s["h"], s["fmt"], rgb555_to_rgb(d[s["px"]:s["px"] + s["size"]])))
        elif s["fmt"] == 0x08 and "pal" in s:
            lut = pal8_lut(d[s["pal"] + 4:s["pal"] + 4 + 4096])
            out.append((s["w"], s["h"], s["fmt"], pal8_to_rgb(unswizzle8(d[s["px"]:s["px"] + s["size"]], s["w"], s["h"]), lut)))
    if not out:
        raise ValueError("no decodable sub-block (see docs/DATA-FORMATS-SH3.md)")
    return out


def map_sections(d):
    """.map payload -> (inner_off, top-level section list). Section dicts:
    off/hdr/size/type/kids. Top-level sections are contiguous; nested blocks
    recurse into the payload and siblings link via the `next` field (0 = last).
    Leaf sections (hdr 0x40) hold a triangle list of 0x24-byte records
    (pos3/normal3/uv2/rgba) and type == vertex count."""
    if d[:4] != b"\xff\xff\xff\xff":
        raise ValueError("not a ffffffff container")
    inner_off, = struct.unpack_from("<I", d, 0x10)

    def block(off, end):
        f0, hs, sz, _z, typ = struct.unpack_from("<5I", d, off)
        if hs not in (0x20, 0x30, 0x40) or sz < hs or off + sz > end or typ > 0x100000:
            return None
        s = dict(off=off, hdr=hs, size=sz, type=typ, kids=[], next=f0)
        if hs == 0x40:
            s["nverts"] = (sz - hs) // 0x24
            s["mat"] = struct.unpack_from("<I", d, off + 0x18)[0]
        else:
            coff = off + hs
            while coff + 0x14 <= off + sz:
                b = block(coff, off + sz)
                if b is None:
                    break
                s["kids"].append(b)
                if b["next"] == 0:
                    break
                coff = b["next"]
        return s

    secs, off = [], 0x50
    while off + 0x14 <= inner_off:
        b = block(off, inner_off)
        if b is None:
            break
        secs.append(b)
        off += b["size"]
    return inner_off, secs


def cmd_map(args):
    archives, _ = load_all(args.data_dir)
    a = archives[args.archive]
    idx = a.find(args.path) if args.path else int(args.idx)
    d = a.payload(idx)
    inner_off, secs = map_sections(d)
    def show(ss, depth):
        for s in ss:
            if s["hdr"] == 0x40:
                print("  " * depth + f"@{s['off']:#x} verts={s['type']:#x} "
                      f"mat={s.get('mat'):#x} size={s['size']:#x}")
            else:
                print("  " * depth + f"@{s['off']:#x} type={s['type']:#x} "
                      f"size={s['size']:#x}")
                show(s["kids"], depth + 1)
    show(secs, 0)
    print(f"inner container @{inner_off:#x}:")
    for j, (w, h, fmt, rgb) in enumerate(decode_container(d[inner_off:])):
        dest = os.path.join(args.out, f"{a.name}_{os.path.splitext(os.path.basename(a.entries[idx].path))[0]}_{j}.png")
        _png_save(dest, w, h, rgb)
        print(f"  page {j}: {w}x{h} fmt {fmt:#x} -> {dest}")
    return 0


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
            kind = {0x08: "8bpp-pal", 0x10: "RGB555"}.get(fmt, "BGRA32")
            print(f"  {name}[{j}]: {w}x{h} {kind} -> {dest}")
            done += 1
    print(f"{done} decoded, {skipped} skipped")
    return 0 if done else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["validate", "ls", "extract", "sniff", "tex", "map"])
    ap.add_argument("data_dir")
    ap.add_argument("archive", nargs="?")
    ap.add_argument("idx", nargs="?")
    ap.add_argument("-o", "--out", default=".")
    ap.add_argument("--path")
    args = ap.parse_args()
    return {"validate": cmd_validate, "ls": cmd_ls, "extract": cmd_extract,
            "sniff": cmd_sniff, "tex": cmd_tex, "map": cmd_map}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
