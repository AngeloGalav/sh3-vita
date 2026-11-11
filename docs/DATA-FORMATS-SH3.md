# SH3 data formats (J3)

Reverse-engineered 2026-11-11 against the local GOG copy (`Silent Hill 3/data`,
oracle `game/sh3.exe` SHA256 `8ff8f748…`). Tool: `tools/j3_arc.py`
(`validate` / `ls` / `extract` / `sniff` / `tex`).

## `<name>.arc` — resource archive (SOLVED, all 30 validated)

- header (16 B): magic `07 05 03 20` | u32 count | u32 aux_size | u32 0
- records (16 B x count): u32 offset | u32 aux_ptr | u32 size | u32 size2 (== size)
- aux table (aux_size B): all zeros in shipped files (runtime scratch)
- payloads: concatenated, `offset[n]+size[n]==offset[n+1]`, last end == file size

2,796 entries, 3.10 GB across 30 archives. `j3_arc.py validate` prints ALL OK.

## `arc.arc` — path table (SOLVED)

gzip-compressed; magic `17 04 03 20`. Records: u16 type, u16 reclen (incl. those
4 B): type 1 = stats (n_archives, n_paths); type 2 = archive stem + entry_count;
type 3 = u32 index `(archive<<16)|entry` + NUL path.

## `ffffffff` container — .tex / .map / .tbn2 / .sbn payloads (structure SOLVED)

Main header at 0x00 (0x20 B):
`u32 ffffffff | u32 0 | u32 hdr_size (0x20) | u32 total | u32 0 | u32 sub_count | u32 0 | u32 0`
(`total` = offset of the LAST sub-header, not the file size).

Sub-blocks start at 0x20. Each sub-header:

```
+0x00 u32 ffffffff | u32 0
+0x08 u16 w | u16 h | u8 fmt | u8 tag (0x20/0x30/0x50 seen) | u16 0
+0x10 u32 pixel_size | u32 STRIDE_TO_NEXT_SUB | u32 0 | u32 marker 0x99990909
```

The stride field is authoritative: header size = stride - pixel_size (0x60 for
the first sub of GB files, 0x80 for the rest, 0x50/0x60 in pic .tex). Pixels
start at `sub + stride - pixel_size`. Do NOT assume a fixed 0x60 stride — that
was a wrong lead from 2026-11-11 morning.

Inventory over all 383 `.tex` entries: sub_count 1 x103, 7 x17, 4 x1, 2 x3,
259 without the container magic at 0 (pic .tex with a 0x40-byte zero preamble
before the container, or 32/96-byte stubs). fmt histogram: 0x20 x86, 0x08 x37,
0x10 x1.

### fmt 0x20 — 32-bit BGRA linear (SOLVED)

`pixel_size == w*h*4`. Room textures render correctly with a B<->R swap
(same fmt value means BGRA in the SH2 converter, `sh2conv/ui.py`). Verified on
room `*TR.tex` and on the GB atlases below.

### `data/tmp/<room>GB.tex` — room-family texture atlas (SOLVED 2026-11-11)

One per room family (amGB, bcGB, brGB, … 17 files), sub_count=7, all fmt 0x20:
6 x 512x512 + 1 x 512x1024 (last sub, marker `0x99990a09`), pixel total exactly
0x800000. These are **color texture atlas pages** (walls, floors, props, posters
— visually verified on brGB: `build/j3-tex/brGB/brGB_L*.png`), NOT lightmaps —
the "8-layer lightmap" hypothesis from the morning session is refuted; the "8
layers" were 6 pages + one double-height page. Room `.map` files reference UV
regions inside these pages (not yet traced).

### fmt 0x08 — 8-bit, palette mapping UNSOLVED

`pixel_size == w*h` (8 bpp). Example `data/pic/br/br32.tex`: 2 subs of
512x512 fmt 0x08 (tags 0x30 then 0x20); after each sub's pixels a 0x1030 block:
u32 `0x1000`, then 256 BGRX entries (X=0x80), then 0x2C bytes.

Direct `pal[index]` is **rejected**: only 64 of 256 palette entries are defined
(four 16-entry gradient ramps at entry offsets 11..26, 75..90, 139..154,
203..218 — i.e. 16 keys per 64-entry block), and only 17% of pixels land on a
defined entry; indices 0..255 all occur, roughly uniformly. Quadrant
(4x256x256) and simple bit-field mappings (`i>>2`, nibble swap, `i&0x3F`) were
tested and render as noise or flat black. SH2's analogous PC paletted .tex
(`sh2conv/pics.py:load_pc_paletted`) uses a dense 256-entry RGBA palette read
directly — SH3's sparse ramp palette does not match that scheme.

Next step for fmt 0x08: read the tex parser in the exe. Useful anchors:
- suffix table `.map .cld .uni .ded TR.tex *demo GB.uni GB.cam 01TR.tex GB.tex
  *movie` at VA 0x6910a0..0x69111c; referenced from FUN_0049bee0 (0x49bee0,
  room-file manifest builder — classifies files, NOT the tex decoder) and
  FUN_0049d6f0.
- the container marker `0x99990909` does NOT appear as an immediate in the exe;
  SH2's container magic `0x19990901` doesn't either — the parser likely builds
  or compares it differently.
- `.arc` magic immediate at VA 0x608e61 (FUN_00608e10, archive reader);
  `arc.arc` magic at 0x60937a (FUN_006092d0).

## Other formats (status from the morning session, unchanged)

- `.map` shares the ffffffff container (room geometry); `.cam`/`.cld` are float
  tables. `.tbn2` (effects) uses the container with fmt 0x08.
- `.000` movies: unknown format, open risk.
- ADX audio: not started (J5).

## Tooling notes

- `j3_arc.py _png_save` had a writer bug (color type 2 with 4-byte rows) — all
  PNGs produced before 2026-11-11 ~14:00 were unreadable; fixed, outputs
  regenerated.
- `cmd_tex` now decodes every fmt 0x20 sub of a container to
  `<archive>_<name>_<i>.png`; multi-sub containers no longer skipped.
