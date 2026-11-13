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
was a wrong lead from 2026-11-11 morning. For fmt 0x08 the stride covers
header+pixels only; the palette block (below) sits between the pixels and the
next sub-header and is NOT included in the stride.

The u32 `sub_count` at +0x14 is unreliable (0 for some single-sub pic files,
e.g. `cu/cudf_mem.tex`); walk subs by the `ffffffff` magic instead, ending at
the first non-magic dword. Some pic .tex files carry a 0x40-byte zero preamble
before the container. The sub marker at +0x1C varies (`0x99990909`,
`0x99990a09`, `0x99990508` seen) and is never compared in the exe — ignore it.

Inventory over all 383 `.tex` entries (recounted 2026-11-11 with the corrected
walk): 370 parse as containers, 13 do not (32/96-byte stubs). Sub fmt
histogram: 0x08 x203, 0x20 x192, 0x18 x78, 0x04 x3, 0x10 x2. Of the 203 fmt
0x08 subs, 198 carry the palette block; the 5 without it are all dev files
under `pic/_work/`. (`pic/lo/logo.tex` has a palette but is flat index-0.)

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
regions inside these pages via their mesh material ids (see the `.map` section;
page selection: see "Texture page rule"). Note ccGB.tex mixes fmt 0x10 and 0x20
pages.

### fmt 0x10 — 16-bit RGB555 (SOLVED 2026-11-11)

`pixel_size == w*h*2`. Little-endian A1R5G5B5: bit15 set on every pixel, R =
bits 10..14 (top 5 after the sign), G = 5..9, B = 0..4; expand 5-bit with
`v<<3 | v>>2`, 6-bit n/a. Evidence: `data/tmp/ccGB.tex` pages 0 and 2 are fmt
0x10 and decode to a coherent street atlas (STOP sign red, green-screen patch
pure green → R-first order confirmed); the same fmt appears as inner pages of
room `.map` files (cc09) with identical structure. 36 of the 2,216 inner map
subs and 2 of the 383 tex subs use it.

**Bug fixed 2026-11-11 (cc colours):** `rgb555_to_rgb` expanded G with `(g<<2)|(g>>4)` (max ~126), halving green on every fmt 0x10 page and giving the purple cast seen on `cc` rooms. Now `(g<<3)|(g>>2)` like R/B; mean RGB of ccGB pages 0/2 went from (41,21,36)/(103,51,100) to neutral, and cc09 renders as grey/olive asphalt with yellow road markings. PNGs made before this fix for fmt 0x10 pages have a magenta tint.

### fmt 0x18 — 32-bit BGRA linear (SOLVED 2026-11-11)

`pixel_size == w*h*4`, same layout as fmt 0x20 (B<->R swap to RGB). This is the
format of the cutscene/puzzle stills in `data/pic/<dir>/` (e.g. `cu/cudf_mem.tex`
renders as the note still). The 0x18-vs-0x20 distinction is not pixel layout
(both 32 bpp) — likely usage/alpha handling; open question, not blocking.

### fmt 0x08 — 8-bit indexed, 16x16 palette (SOLVED 2026-11-11, data side)

`pixel_size == w*h`. After each sub's pixels sits a **0x1030-byte palette
block**, NOT covered by the stride field:

```
u32 0x1000 (palette byte size) | 1024 RGBX entries (R = first byte) | 0x2C zeros
```

**CORRECTED 2026-11-11 (exe-confirmed palette swizzle + PSMT8 pixel swizzle).**
Both the palette lookup and the pixel storage use the PS2 GS layout:

```
t     = (i & 0xE7) | ((i & 8) << 1) | ((i & 0x10) >> 1)   # swap index bits 3 and 4
color = pal[(t >> 4) * 64 + 11 + (t & 15)]                # pal = 1024 RGBX u32, R first
alpha = min(255, 2 * X)                                    # X = PS2 alpha 0..0x80
pixels: stored in GS PSMT8 order (32-bit page layout); linear index = unswizzle8(buf, w, h)
```

- **Exe proof (palette):** the 256-byte table at VA 0x6bd840 is exactly the bit-3/4
  swap above; it is read by the palette converters at 0x4921b0 (16 entries, via
  0x493170 for 4bpp) and 0x4922d0 and following (256 entries). They compute
  `entry = (t >> 4) * 64 + (t & 15)` relative to a base pointer (the +11 is part
  of the block layout in the file) and `alpha = min(255, (raw >> 24) * 2)`.
  The earlier "direct" mapping (no swizzle) was WRONG: with the swizzle the mean
  neighbour roughness of 229 palettised images drops 48.5 -> 25.3 (200 of 229
  smoother; fire00 30.7 -> 5.7).
- **Data proof (pixels):** treating the pixel bytes as PSMT8-swizzled
  (`j3_arc.unswizzle8`) makes 263 of 314 fmt-0x08 images smoother (mean
  roughness 34.8 -> 15.7) and turns fire00 from striped bands into a smooth
  flame with the intended ordered dither. The exe-side pixel unswizzle was not
  located (not needed for the conclusion).
- Run pattern: the 16 colors of a row sit at entries r*64+11..r*64+26 (455/455 rows
  in shipping files); every pixel index maps to a defined entry. The X byte is
  0x80 constant in overlay subs and varies 0x00..0x7a in fire textures.
- Where fmt 0x08 is used: only `pic.arc` (screen/item stills: 206 subs) and
  `tex.arc` effects (`eff_tex/*.tbn2`, 24 subs). All room textures (`bg*.arc`) are
  fmt 0x20/0x10, so the room renders are unaffected. The D3D texture creator
  0x461d90 copies pixels only for fmt 0x10/0x18/0x20; 8bpp content is expanded
  to 32-bit by the converters above before upload.
- `j3_arc.py` implements all of this (`pal8_lut`, `unswizzle8`, `decode_container`).
  Item atlas `it/item.tex` unswizzled still shows an interleaved dither pattern.

Superseded leads: direct `pal[index]` rejected (17% coverage); SH2's dense
256-entry scheme (`sh2conv/pics.py:load_pc_paletted`) does not apply.

Exe decoder LOCATED (2026-11-13, confirms the data-derived mapping): the CLUT
expander `0x4921b0` loops over the 256 pixel-index slots; for slot `i` it reads
`t = table[i]` from the swizzle table at `.data 0x6bd840` (a 256-byte PS2 CLUT
bits-3/4 swap: `[0..7,16..23,8..15,24..31,…]`, identical to the tool's inline
`t = (i & 0xE7) | ((i & 8) << 1) | ((i & 0x10) >> 1)`), then indexes the palette
as `pal[(t>>4)*64 + (t&0xf)]` and expands the RGBX entry (alpha via
`min(255, 2*a)`, matching the PS2 0..0x80 alpha note). The tool's extra `+11` is
the raw-file-block per-row padding (defined colors sit at row offsets 11..26 in
the 1024-entry block); the exe runs on a loader-relaid-out palette whose row base
already skips it, so both hit the same colors. Companion pixel-format converters:
`0x492220` X8R8G8B8→R5G5B5A1, `0x492280` X8R8G8B8→R4G4B4A4. The earlier "marker
`0x99990909` absent / no xref to the path table" leads are moot — the converter is
reached through the texture-format dispatch, not a string/marker xref.

## `.map` — room file (STRUCTURE SOLVED 2026-11-11)

287 entries (2.38 GB) inside the `bgXX.arc` archives at `data/tmp/<room>.map`.
All 287 parse with the layout below (validated end to end).

Outer container header (0x50 B):
`ffffffff | 0 | 0 | 0x50 | u32 inner_off | 0 | 0x50 | u32 L1_off | ...`
This header is the exe's own **model header** (object init FUN 0x44f790 reads it;
see "Texture page rule" below): `+0x18` = 0x50, `+0x1C` = offset of list L1,
`+0x20` = list L3, `+0x24` = list L2, `+0x30` = texture container offset (equals
`inner_off`), words `+0x42/+0x44/+0x46` = section counts of L1/L2/L3.

- **Geometry blob** `0x50 .. inner_off`: a tree of sections. Section header:
  `u32 next | u32 hdr_size | u32 size (incl. header) | u32 0 | u32 type | ...`
  Top-level sections are contiguous; blocks inside a wrapper start at
  `payload_start` and siblings link via `next` (absolute offset, 0 = last).
  Wrapper types seen: 0, 1, 2, 3, 4, 8, 9, 10, 256, 265, 0x10000..0x1000f.
  Typical chain: `type 3 → type 0x10000+k → type 9/0` wrapping the mesh list.
- **Type-1 section** (hdr 0x20, size 0xe0): 4x4 identity matrix + 9 bbox
  corner floats (room bounds in world units, e.g. mrc3 x∈[-141800,-130300],
  y∈[-2000,0], z∈[100000,107600]).
- **Leaf sections** (hdr 0x40): `type == vertex count`; body at +0x18 =
  material id, +0x1C = 0x3c (constant). Payload = triangle list (degenerate
  filler triangles present) of 0x24-byte records:
  `float3 pos | float3 normal | float2 uv | u32 rgba` (color bytes R,G,B,A).
  33,261 of 33,271 children match `type == (size-0x40)/0x24` across all 287
  maps; the 10 outliers are unexamined. Material ids run 0..~67 in blocks of
  16/32 (e.g. cc09: mats 0x0..0x7 @120 verts, 0x10..0x17 @240, 0x20..0x27
  @150) and are NOT texture ids (they feed vertex-shader constants; see Texture page rule).
- **Inner container** at `inner_off`: standard ffffffff container (sub_count
  reliable here) holding the room's own texture pages — usually 5–12 ×
  512x512 fmt 0x20 BGRA (first sub tag 0x30, rest 0x50), sometimes fmt 0x10
  RGB555. The last sub's pixels end exactly at EOF in all 287 files. Example:
  mrc3's single page decodes to white wall + tile-band textures with red
  Japanese "ここ" dev annotations baked in.

Tool: `j3_arc.py map <data> <archive> --path data/tmp/<room>.map -o OUT` prints
the section tree and writes the inner pages as PNGs.

### Texture page rule (RECONCILED 2026-11-13: page is STORED verbatim, no parser)

Two earlier write-ups (the 2026-11-11 static "dw5" rule and the 2026-11-13 runtime
trace) describe the **same records**; this is the single reconciled rule. The
`.map` is loaded **verbatim** (cache read-by-type `0x49c660`→`0x600020`; live bytes
== file bytes, proven on tpe2) and the BG-init consumer `0x44f790` walks it in
place — there is **no file→slot transform parser**.

The consumer walks three **channel lists** at `base+[base+0x1c]` (count u16
`base+0x42`), `base+[base+0x20]` (count `base+0x46`), `base+[base+0x24]` (count
`base+0x44`). Records are **0x30 B**, interleaved with their vertex payload,
chained by **`+0x08` (size)** — next sibling = `rec + [rec+0x08]`; `[rec+0x00]==0`
terminates. **Group**: `+0x04` child (rel → first leaf), `+0x10` type, `+0x14` s32
**texid**, `+0x18` u16 leaf count. **Leaf**: `+0x10` s16 **pageidx**, `+0x16` u16
flag (clamped 0..3); leaves chain by `+0x08` (size) like all records.

**THE RULE:** a leaf samples **page `leaf.pageidx` inside the texture object
`group.texid`**, both read verbatim. Runtime resolution: `texobj = 0x45bff0(texid)`
(global tex table `0x867d44+texid*0x10`), `page = 0x45b200(texobj, pageidx, 1)` =
`texobj+0x88+pageidx*0x30` (page descriptor tag `0x7000+pageidx`). Handlers that
resolve each batch's page: `0x44f1b0` (type 1, global-tex), `0x44f4f0` (type 3,
room-tex), `0x44ed70` (type 2, color). `0x458010`/`0x457f30` only size/pack
vertices (section-tree leaf `+0x10`=nverts, `*0x24` B) and never touch texid/pageidx.

**Reconciliation of the 2026-11-11 "dw5" table.** The section `dw5` (u32 at
section+0x14) **is** the group `texid` at `+0x14` — same field. The old table's
"type 1→GB / type 2→TR / type 3→inner" was a **per-room heuristic**, not a
structural law: `texid` is a per-room handle into the global tex table, so which
`.tex` file it points at (GB atlas / room TR / inner container) depends on what
that room loaded; the `type` field selects the **render channel/handler** (blend
mode), not the texture file. Counterexample that forced this: cc09's type-2 group
carries texid 0 (the GB object) pages 1,2,3 — a second GB render pass, and cc09
has **no** TR. The earlier observation "type-1 `dw5` < GB sub count" still holds
because type-1 texids index the GB pages. The wrapper `k` (type `0x1000k`) remains
a label, never a page index. Exe facts retained: `SetTexture(0,...)` once per
top-level section (`0x4590e0`/`0x459150`/`0x4591c0` → `0x458c30`); GB set =
`tex(G)` `0x867d00` (loader `0x5e8f70`), TR set = `tex(T)` `0x867530` (`0x5e8fb0`).

**UV space check (2026-11-13, closes the open question).** Over 861k cc-family
section-tree vertices, UVs lie in [0,1) except 90 values (0.01%, edge/wrap
artifacts): **no UV tiling**. The 512×1024 GB sub is **one page** sampled whole
(the "two 512² tiles" look is content, not two pages), and `pageidx 3` (that
512×1024 page in `ccGB.tex`) **is** referenced (60× across cc L1 groups) — the
earlier "cc idx 3 never referenced" note is refuted. Page-list order: inner / TR /
GB are separate texture objects; `texid` picks the object, `pageidx` the page
within it (container sub order, tag `0x7000+idx`); there is no single mixed list.

Open (page rule): lighting (vertex color may carry it); fmt 0x04 (see below,
parked). The geometry draw/texture path and the file loader are both located now.

### Leaf outliers EXPLAINED (2026-11-11)

Re-walk of all 287 maps through the three header lists: 33,739 leaves, 478 do not satisfy `size == 0x40 + n*0x24`. They are all in rooms `cuf3`, `mu12`, `mu22`, `mu2d`, `mu2f`, `mu32`, `muee`, `muf1` (plus a few more mu*). 464 sit under a type-**10** intermediate, 4 under type 9 (cuf3), 10 under type 0 (mu2d, muee). Each still starts with `n` normal 0x24-byte vertices; after them comes an auxiliary block: leaf header `dw8` (type 0/9 leaves) or `dw9` (type 10 leaves) = `16*(n+4)` bytes of per-vertex auxiliary data, and the total extra after the vertices is `4*n` (+0..28 alignment padding) bytes for type 10 (words like `0x80131414`, `0x80139314`: packed per-vertex values, probably a second colour/light term) or larger blocks for type 0/9. The auxiliary data is not geometry: a renderer can read `n` vertices from the leaf and ignore the rest.
Script: `sh3-vita/build/map-scratch/outl.py`.

### fmt 0x04 — 4bpp indexed (runtime path SOLVED 2026-11-13; file-side pixel swizzle OPEN)

Only `pic/mp/mpmchurch1_10_tr.tex` and `mpmchurch2_10_tr.tex` (both 512x1024).
The earlier claim that `pic/test.tex` was fmt 0x04 was WRONG — its sub header at
file+0x60 reads fmt 0x08 (512x512); the `09 09 99 99` marker sits at sub+0x1c.
The old palette claim (`00000080 09090980 …` after the pixels) does not exist in
any file — superseded by the block layout below.

Container (validated on mpmchurch1_10_tr, 266480 B):
- sub1 @96: `+0x08` u16 w (LE), `+0x0a` u16 h, `+0x0c` fmt, `+0x0d` pixoff (0x30),
  `+0x10` u32 pixel size (w*h/2 = 262144), `+0x14` u32 next-sub stride (0x40060),
  `+0x1c` marker (`09 0a 99 99`). Pixels at sub+0x30.
- sub2 @ sub1+stride (262336): the **0x1030-byte CLUT block**, same format as
  fmt 0x08: u32 0x1000 marker at +0, then 1024 RGBX u32 entries at +4. Defined
  colors sit at row offsets 11..18 of rows 0 and 1 (dwords 11..18 and 75..82) —
  8 colors per row, 16 total. Entry = (R,G,B,X) with **alpha = min(255, 2*X)**
  (PS2 0..0x80 alpha). Row 0 = nibble indices 0..7, row 1 = 8..15.

Runtime decode (exe `0x493160`, the fmt-4 path; calls the CLUT expander
`0x4921b0` with count 0x10):
- CLUT: `lut16[i] = expand(pal_runtime[swiz(i)])` where `swiz(i)` uses the same
  0x6bd840 bit-3/4-swap table as fmt 0x08; for 16 entries this maps i=0..7 →
  runtime dwords 0..7 and i=8..15 → 64..71, i.e. exactly the file's rows 0/1
  offsets 11..18 after the loader relays the block (skips the +11 row pad).
- Pixels: each source byte expands to two 32-bit pixels, **low nibble first**
  (`0x493160` builds a 256-entry byte→2×u32 LUT from lut16 and walks the source
  linearly row by row). So the buffer the decoder sees is LINEAR — any GS
  swizzle is undone earlier in the loader (same situation as fmt 0x08, whose
  pixel unswizzle was also never located in the exe).

OPEN (file-side pixel order): none of linear, textbook PSMT4 (64x16 blocks, all
intra/block variants that fill 512 B), transposed-linear, or the `unswizzle8`
dword permutation adapted to 4bpp produce a coherent image (2D luminance
continuity stays ~11-13 in every variant; only a non-filling transposed variant
scores 5.7 vertically but 14+ horizontally). The two files look like unused PS2
leftovers. Next concrete step if ever needed: runtime-oracle capture — break at
`0x493160` in sh3-nx, dump src+dst, diff against the file bytes to derive the
loader's permutation directly.

### `.cam` / `.cld` / `.ded` / `.kg2` survey (sizes only, 2026-11-11)
- `.cam` (285 files, 512-3328 B): size is a multiple of 128 in all files => fixed 0x80-byte records; each starts with 3 float pairs + 3 float pairs (XZ points of a trigger/camera area), then 50.0, -1600.0 and small ints; semantics not decoded.
- `.cld` (288 files): SOLVED, see "`.cld` collision" below.
- `.ded` (285 files): SOLVED, see "`.ded` - per-room light list" above.
- `.kg2` (280 files, 0-123616 B): loader 0x5e8d20 stores the pointer per channel at [0x7f4c40+4*ch] (flag [0x7f4c08]); the only consumer (0x4877d0, called from 0x489800) registers up to 4 objects with 0x78-byte entries. Content looks like compact geometry: 16-bit fixed-point positions and +-1.0 normals as s16 (0x7fff/0x8000) with small vertex counts (6, 8 ...), i.e. shadow/prop meshes ("kage"), NOT lights. Not decoded. *(Superseded 2026-11-13: container solved via `0x4ccd90`; runtime use = per-frame light/effect display queue — see "`.kg2` / `.lv` container SOLVED" below.)*
- `.lv` (4 files): loader 0x5e8d90, pointer at [0x7f4c7c+4*ch] read only by 0x4d1067; starts with a 4x4 matrix and small index/vertex data; not decoded.
- Counts over all arcs: tex 383, mes 368, cld 288, map 287, cam 285, ded 285, kg2 280, mdl 157, anm 92, sdb 81, kg1 69, tbn2 34, pic 32 (+hd/bd/td 32 each).

### `.mes` message files (runtime path SOLVED 2026-11-13, exe `0x496780`/`0x5fa400`/`0x5f9da0`/`0x5ff8a0`; earlier tool `tools/j3_mes.py`)

`msg.arc`: 368 files = 46 areas x 8 languages (`<area>_<lang>_msg.mes`). Loader `FUN_00496780` formats `data/msg/%s_%s_msg.mes` from an area-name table (`PTR_DAT_006bdbf0`) and a language-name table (`PTR_PTR_006bdcac`), loads into one of two buffers (`0xb577f0` normal, `0xb61800` for areas 2/3/4) and selects the font set via `0x5fd9d0`: lang 6→0, 7→2, else→1, matching `fontdata_c.bin` / `fontdata_j.bin` / `fontdata_k.bin` (strings at `0x6995d8..`).

- **Container (getter `FUN_005fa400(buf,id)`):** `buf` is u16-LE; `buf[0]` = message count n; `buf[1..n]` = message offsets **in u16-word units** from the buffer start; message address = `buf + 2*buf[id+1]`. (The old "T0[0] = byte size / group records of pointers" reading was wrong — `T0[0]` is the message count, e.g. 0xd7 = 215 messages in amusement_east.)
- **Text stream grammar (measurer `FUN_005f9da0`):** a byte-oriented code stream. Item prefix rule: byte `< 0xe0` = 1-byte code; byte `== 0xe0` + u16-LE = 3-byte extended code; byte `> 0xe0` = 2-byte **big-endian** u16 (`0xff 0xff` = 0xffff = end of message). Codes: `0x00` = space (space width); `0x01..0xdf` = glyph index into the per-language font width table (`[fontobj+0x10+code]`, `DAT_070a22c8[lang]`); `0xfd xx` = kern +xx; `0xfc xx` = kern −xx; `0x8000` = style/shift marker; `0xfffd` = line break (pushes the accumulated line width to the output array); `0xfffe` = full-width space; `0xffe0+n` (n=0..9) = **argument slot**: splice in the pre-compiled message at `DAT_070c9cbe + n*0x80` (slots filled by `0x5fc230` from `0x5ff8a0`-compiled strings — item names etc.). Verified decode of amusement_east (215 messages): capitals appear both as ASCII slots (`0x41..`) and stored 0x20 low (`0x21..0x3f` → `+0x20`, e.g. `0x29`='I', `0x34`='T'); `0x80` = sentence-start marker; `0x07` = apostrophe glyph; `0x0e` = line-end glyph; `0xff03`/`0xff00` = item-name open/close (the old "0xff 0x03 before item names"). Sample: `␣{80})T{07}S␣LOCKED{0e}` = "IT'S LOCKED".
- **Runtime string compiler `FUN_005ff8a0`** (for `sprintf`'d strings/args, not `.mes` bytes): ASCII → glyph code `c − 0x20`; specials: `0x0a`/`\n` = newline (−3), `0x20` = space (−2), `0x1b` ESC sequences (`k`/`w#`/`a#` = wait-key/wait-time/align codes 0x1000/0x2000+n/0x3000+n), backslash escapes `\n \+ \- \l \c \a \x# \y# \h \z \<digits> \s \d \b \v# \_ \p+k/w/a \e(=ESC) \!(=h) \\(='<') \|`, CJK: lead byte ≥ 0x80 pairs with the next byte → `0x5ff6f0(lead*0x100 + lo)` glyph lookup (the JPN/KOR/CHN path, font per `fontdata_{j,k,c}.bin`). Output: 128-short glyph-code array per message slot, −1 = end (+0x8000 trailer word).
- **Still open:** the exact font-table base reconciliation between the `.mes` byte codes (used directly as width-table indices) and the `−0x20` convention of `0x5ff8a0`; the `0x8000`/`0xe0`-extended code semantics (style vs CJK) need a CJK-file trace; `0x07`/`0x0e` are plain glyph slots in this model (apostrophe/line-break glyph positions in the atlas), not escapes.

### Lighting model (exe-derived 2026-11-11; light DATA source not located)

The room geometry is lit in the vertex shader, not baked into the texture. The exe contains the shader source as text in `.data`/`.rdata`: pixel shaders (ps.1.1) at 0x68f300-0x68ff1c (+0x6b1278), vertex shader pieces at 0x6b4958-0x6b6b70 and 0x6b84c8; the vertex shader is ASSEMBLED at runtime from snippets (builder FUN 0x45f930 / mode selector 0x45f740; per-leaf mode from the table 0x6b4670).

Per-vertex lighting (static room VS, assembled): `r0 = sum(light_i contribution)`; point lights i=0..3 at constants c16..c19 (colour c20..c23, attenuation scale/bias c24/c25, `contribution = max(dot(N,-L),0) * clamp(k*rsq(d)+b)`), spot lights c32..c35 (direction c36..c39, colour c40..c43, cone/atten c44..c47), ambient `c48` (`add r0,r0,c48`), then **`r0 = r0 * v5`** where `v5` is the per-vertex colour (the leaf's last dword, bytes R,G,B,A), written to `oD0.rgb = min(r0, 1)`; a variant uses `mad r0, v5, c52, r0` with c53/c54 (second diffuse `oD1 = v5 * x`). Fog from `oFog` (c0/c1/c26/c27). World matrix of a leaf: constants c8..c10 (rows) found by the leaf `mat` (grid cell record, FUN 0x453550); view-projection c2..c5.

Pixel shader (most used): `tex t0; mul r0.rgb, t0, v0; +mov r0.a, t0`, i.e. final colour = texture * vertex-lit colour. Lightmap/flash variants add `t3` (projected light texture): `add_sat r0, v0, t3*2*v1; mul r0.rgb, t0, r0`; shadow variants lerp/subtract `t2` by `c2/c3.a` or `v1.a`. ~35 pixel shaders exist for the combinations.

So the per-vertex colour is a MULTIPLIER on the dynamic light sum, not a standalone colour: in the shipping rooms most vertices are `(38,38,38,255)` (am11 40%, bcf1 72%), some `(255,255,255,255)` (fixtures), cc09 is uniformly `(179,179,179,0)`. Static approximation consistent with the shaders: `lit = tex * clamp((ambient + sum(lights)) * vertex_colour)`.

### `.ded` - per-room light list (SOLVED 2026-11-11, exe-derived + position check)

`data/bg/<fam>/<room>.ded` (285 files, 384-1712 B). Loader callback 0x5e8ff0 (label `ded(I)`) copies the file to the global at VA 0x742ac8 (`0x601160` = memcpy) and FUN 0x43b600 processes it (if no file is loaded it uses the built-in default block at VA 0x6b2018 with header id 0xffffffff: one 32-byte record `(0.02,0.02,0.02,0 | 1,0,0,type 1)` plus a second one). Layout:

```
+0x00 u32 room id  ((area << 16) | room, must match the current room or the exe logs 0x68f07c)
+0x04 10 x (u32 count, u32 offset)   # categories 0..9, records at those file offsets
+0x54 u32 body offset                # common parameter block after the light records
records start at 0x70
```
Categories seen (count, record size): cat0 (0-3, 32 B): directional/ambient lights = `colour rgb,a | direction xyz, int type` (the VS directional snippet uses c8..c11 directions and c12..c15 colours, max 4-5); cat1 (0-1, 1168 B), cat2 (0-3, 48 B), cat3 (64 B), **cat4 (0-5, 48 B): point lights** (VS point lights c16..c19 / colours c20..c23; the exe's "add light" helper 0x43c110 caps it at 5 and initialises `colour (1,0,1,0.1), near 2000, far 3000, type 0xa`), cat5 (48 B), **cat6 (48 B): second light class, type 0xc** (helper 0x43c1a0, cap 2; VS spot snippet c32..c35 / c36..c39 dirs / c40..c43 colours / c44..c47 cone), cat7 (64 B), cat8 (320 B), cat9 (64 B). 48-byte point/spot record (12 floats): `[0..3] colour (values above 1 occur, e.g. 3.0/2.6/2.2), [4] near, [5] far, [6..7] unused, [8..10] WORLD position, [11] int type` (0xa point, 0xc spot).
- **World position:** the room's local geometry is offset by the translation row of the 4x4 matrix in the first section of the `.map` (file +0x18 -> section type 1 at 0x50: floats 12..14, e.g. am11 and amf1 = (20000, 0, ±20000)); local = ded position - that origin. Verified: am11 light (21450,-1000,18720) -> local (1450,-1000,-1280), inside the room bounds x[0,4450] y[-1600,0] z[-3200,48]; with ambient 0 a rendered lamp ring appears at that light.
- **Attenuation** (VS: `mad r8.z, c24.x, 1/d, c25.x` then clamp 0..1) = `a/d + b` with `a = near*far/(far-near)`, `b = -near/(far-near)` (1 at `near`, 0 at `far`) - derived from the shader form, not read from the upload code.
- **Body block (offset in +0x54):** 10 vec4 = `0x90: (0.15,0.15,0.15,31.5) (0.045,0.045,0.045,0) (22.4,22.4,22.4,1) (4,4,4,117.55) (0,0,0,124) (15000,5555,0,255) (0,0,0,1) ...` identical in most rooms (flashlight/shared parameters); the exe copies 0xa0 B to 0x743af8, 0x30 B (+0xa0) to 0x743b98, and single dwords (+0xd0..+0xe8) to 0x743bd0..0x743bf8; fog distances such as 15000/5555 or 7500/3000 (last vec4 component 255) differ per room. Exact meaning of each body field not decoded.
- Header id examples: amf1 = 0x000100f1, am11 = 0x00090011, us1f = 0x0001001f.
- Ambient: the VS adds `c48`; its source is the cat0/default light data (default 0.02 grey); not exactly pinned. Rendering check: `sh3-vita/build/map-scratch/render_lit.py <room> <gain> <ambient>` (am11/am1f/bcf2). The rooms are meant to be dark (flashlight-lit): the static lamp lights only brighten fixtures within `far` (400-2300 units).

### Per-room resource loaders (exe, 2026-11-11)

Resource-type descriptor table at VA 0x71ec00-0x71f400 (0x38-byte entries; `[+0x1c]` = label string, loader/unloader function pointers just before it): `map(I,0..3)` load 0x5e8ca0 -> model object loader FUN 0x44f9b0 (the `.map` outer header is the model header, see "Texture page rule"); `kg2(I,0..3)` 0x5e8d20 -> pointer stored at [0x7f4c40+4*ch]; `lv(I,0..3)` 0x5e8d90 -> [0x7f4c7c+4*ch]; `cld(I,0..3)` 0x5e8dd0 -> FUN 0x4adec0 (collision); `uni(G/I)` 0x5e8e10 -> 0x5e9cd0/0x5e9cf0; `cam(I,0..3,G)` 0x5e8f20 -> stores [0xbcc238+4*ch] and calls the camera fix-up 0x5a1350; `tex(G)` 0x5e8f70 / `tex(T)` 0x5e8fb0 -> texture sets 0x867d00 / 0x867530 (FUN 0x45c080 / 0x45c0c0); `ded(I)` 0x5e8ff0 -> memcpy to 0x742ac8 + FUN 0x43b600 (light list).

### `.cam` (partly): the camera fix-up 0x5a1350 walks 0x80-byte records starting at structure+0x40 (stride 0x80, loop continues while bit 0 of the byte at record+0x84 is clear) and patches sentinel floats (10001.0 = 0x461c4000, 10002.0 = 0x461c4400, 10003.0 = 0x461c4800) into defaults (-300, -375, -600 ...), so the files hold per-record camera parameters with "use default" sentinels. Field semantics not decoded.

### `.mdl` (decoded 2026-11-12, J3 item b, tool `tools/j3_mdl.py`, verified on all 157 files)

`data/pcchr/<group>/<name>.mdl` (157 files, chr*.arc) is NOT the `.map` section tree. Outer header: `u32 0 | u32 id | u32 nparts | u32 size | u32 size | u32 dataoff | 0 | 1`; part table at 0x40 = nparts offsets (all == dataoff) then nparts sizes (all == size); `dataoff = round_up(0x40+8*nparts, 0x10)`. **`size` is the absolute file offset where the model block ends: the `ffffffff` texture container (j3_arc) starts exactly there, or the file ends there — verified on all 157 files.** Model block length = size - dataoff.

Model block = `ffff0003 | 3` + a fixed 0x80-byte directory (offsets self-relative to the block base; verified on all 157 files):

| slot | count | offset | content |
|---|---|---|---|
| A | +0x0c nA | +0x08 offA (always 0x80) | nA x 0x40 bind-pose bone matrices, row-major 4x4, last row = translation + 1.0 (verified rchhaa/bg_bld) |
| B | +0x14 nB | +0x10 offB | nB bone-parent bytes (0xff = root, else parent index) |
| C | +0x1c cEnd | +0x18 offC | sorted pairs of bone-index bytes, region [offC, cEnd) |
| M | (nB) | [cEnd, offF) | nB x 0x40 float4x4 matrices (second matrix set; size relation verified on all 157 files; semantics inferred) |
| D | +0x20 nD | +0x24 offD | nD per-part (mesh) descriptor records, variable size; first u32 = total D-region size (verified bg_bld); region [offD, offE) also holds per-mesh float vertex/index bulk data (float4 positions with w=1.0 observed; semantics inferred) |
| E | +0x28 nE | +0x2c offE | nE x 0x40 records; region [offE, block end) |
| F | +0x30 nF | +0x34 offF | nF u32, region [offF, offG) |
| G | +0x38 nG | +0x3c offG | nG x 8-byte records, region [offG, next) |
| H | — | +0x44 offH | pointer into A/M region (0 in 115/157 files) |
| I | +0x4c nI | +0x48 offI | vertex stream, 12-byte records = 6 x s16 (pos.x,pos.y,pos.z, nx,ny,nz), normals scale 4096 (verified: all rchhaa normals |n| in [4094.4, 4096.0]). nI = number of J stream descriptors. NOTE: for character models this stream is a shared template shell (815 verts in chhaa AND rchhaa, 823 in chcaa), not the body mesh |
| J | — | +0x50 offJ | nI x (u32 count, u32 offset) stream descriptors; streams live in [offJ+8*nI, offK), each count x 14 bytes (7 x s16; stride 14 verified via stream-size arithmetic on bg_bld + rchhaa). **MORPH TARGET DELTAS — SOLVED 2026-11-13 via runtime oracle** (consumer `FUN_00463bd0`, caught by hardware read-watchpoint in sh3-nx): stream k = morph target k with a per-frame float weight `w = weightarr[k]` (weightarr = `[charobj+0xc]`, float[nI]; zero-weight streams are skipped). Record `(f0..f6)`: `f6` = vertex index into the I stream (sorted, unique per stream); the consumer does `pos[f6] += (f0,f1,f2)·w/16` and `normal[f6] -= (f3,f4,f5)·w/4096` on a global float stride-6 accumulator `DAT_00886e28..e3c` (pos.xyz+normal.xyz per vertex). The runtime J buffer is a copy of the file stream with f3..f5 NEGATED (observed live: file (-739,-504,362) → runtime (739,504,-362)), so net vs file data: `normal[f6] += (f3,f4,f5)·w/4096`. Per-frame chain `0x685e90→0x454670→0x45f5d0→0x464350→0x463bd0`; live chhaa weights e.g. {0:0.004,1:0.274,4:0.274,6:0.054,8:0.063,9:0.230,13:0.510,17:0.347,21:0.347} |
| K | — | +0x58 offK | zeros in the two test files; +0x5c duplicates offK (all 157); region [offK, offD) |
| L | — | +0x60 offL | always offK + 0x10 (all 157) |
| — | +0x64 flags | — | values 1 (94 files), 0x40000001 (33), 0x40000005 (29), 5 (1) |
| — | +0x74 | — | 0 in 148/157, 1 in 9 |

Slots +0x40, +0x54, +0x68..0x70, +0x78, +0x7c are 0 in all 157 files. Every byte of every model block is assigned to one of these regions (asserted by `j3_mdl.py --survey` on all 157 files). Directory shapes (nonzero-slot pattern + flags): 6 distinct — 94 x (no H, flags 1), 28 x (H, 0x40000005), 19 x (no H, 0x40000001), 14 x (H, 0x40000001), 1 x (no H, 0x40000005), 1 x (no H, 5).

Runtime: the movie constructor 0x683240 computes `filebase + [filebase+0x14]` (dataoff) and reads directory slots at runtime (directory itself is used in place). One transform IS applied to the J region when staged (f3..f5 negated — observed live 2026-11-13; the staging site is not yet located). Skinned VS 0x6b57b8 (vs.1.1 ASCII text in .text): `v0` pos, `v1` = 3 blend weights (4th implicit = `c0.x − Σv1.xyz`), `v2.zyxw` = 4 bone indices (scaled by `c0.w`), `v3` normal (renormalized), bone matrices at `c[48+idx]`, final `m4x4` by `c2`. The earlier "stride 0xE at skin-draw site 0x63be0d" was a misread — that push is a `SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX=0xe, 3)` value, not a stream stride. Embedded textures decode with `j3_arc.decode_container(b[size:])` (verified: rchhaa -> 4 textures incl. a 512x1024 atlas with hair/face/teeth/skirt/watch).

### `.anm` character animation (decoded 2026-11-12, J3 item b, 92 files)

Container (verified exactly on 90/92 files): `[u32 id][nframes x stride]`, i.e. **`size = 4 + nframes * stride(char_id)`**. `id` (file +0) equals the paired `.mdl` id (chrpl/chrch 0x100..0x104, chren 0x200..0x215, chrbg/chrit 0x101c..0x104f). The two exceptions (`test_en_shb.anm` remainder 480 < 558, `test_bg_sya.anm` remainder 52 < 146) are `test/`-directory re-encodes of `en_shb`/`bg_sya` (differ from the shipping files in every frame, different frame-0 control word — not byte copies, not truncated): a DP walk of the record grammar (type 7 = skip, raw/half translation both allowed) shows each remainder is a COMPLETE frame record ending exactly at EOF (65–72 and 25–32 bones). So frames occupy `stride` bytes each only as the seek increment; the final frame may be stored at its natural compressed length without trailing padding. The dword at +4 is NOT a header: it is frame 0's first control word (0x999999aa player, 0xaaaaaaaa en_*, 0x22222222 bg_sya, 0x0a it_dagger). Autocorrelation on en_smb.anm peaks at lag 240 from file offset 0, confirming the grid starts at +4. `.anm` paths: `data/chr/<group>/<name>.anm`.

`stride` is a hardcoded per-character constant, exe function 0x473c10 keyed by the char id word `[char+0x80]` (verified: table + jump tables 0x473d74/0x473d88/0x473ddc + byte table 0x473e04):

| id | stride | id | stride | id | stride |
|---|---|---|---|---|---|
| 0x100 player | 0x108 (form 0 via 0x4c9010; forms 1/2 -> 0, >2 -> id) | 0x209 | 0x2e | 0x101f | 0x11c |
| 0x101 | 0 | 0x20a | 0x8a | 0x1020 | 0x46 |
| 0x102 | 0x2de | 0x20b | 0x7a | 0x1024 | 0x92 |
| 0x103 | 0x1e0 | 0x20c..0x20f | 0 | 0x1033 | 0x16 |
| 0x104 | 0x1f6 | 0x210 | 0xf0 | 0x1038 | 0x2e |
| 0x200 | 0xbe | 0x211 | 0xd0 | 0x103d | 0x80 |
| 0x201 | 0x68 | 0x212 | 0x11c | 0x103e | 0x46 |
| 0x202 | 0x90 | 0x213 | 0xd0 | 0x1043 | 0x34 |
| 0x203 | 0xdc | 0x214 | 0x22e | 0x104e | 0x16 |
| 0x204 | 0x96 | 0x215 | 0x116 | 0x104f | 0xa8 |
| 0x205 | 0x86 | 0x101c | 0x22 | (0x101e..0x104f others) | 0 |
| 0x206 | 0xd0 | 0x101e | 0x8a | | |
| 0x207 | 0x74 | 0x208 | 0x22 | | |

Frame counts from the formula (verified by the divisibility check): en_smb 1980 frames @0xf0, player 1763 @0x108, en_bhr 1237 @0x116, en_shb 713 @0x22e, en_hpb 1728 @0xd0, en_nse 1433 @0xdc, en_ap2/apb 1477 @0xd0, en_one 1208 @0xbe, chcaa 30 @0x2de, it_dagger 2 @0x16.

Runtime (exe): the movie loader 0x59cb40..0x59cf40 reads the file raw into the arena at 0xbc9a10 — no header parse. Per-character descriptors live in a runtime linked list headed at 0x7117e50 (lookup 0x682eb0 by char id, alloc 0x682e20): `desc+0x18` = loaded `.mdl` + dataoff (the model block), `desc+0x1c` = loaded `.anm` file. Binder 0x46a7f0(char, desc+0x18, desc+0x1c) builds the bone pool via 0x4a6070(nA, parentbytes) into `char+0x238` (only if empty) and sets `char+0x70` = bone list, `char+0x2d0` = model info block. Char anim slots: main `char+0x238`, secondary `char+0x1b0`; slot+0 = bone list, slot+4 = base (= file+4, `char+0x23c = anm+4`), slot+0x14 = stride (from 0x473c10), slot+0x18 = packed `frame<<12 | subframe` (subframe = interpolation phase, mask 0x80000fff), slot+0x2c/+0x2e = current/next frame words, slot+0x30 = state 0..6, slot+0x40/+0x44 = current/next script entry. Frame addressing `frameptr = base + frame*stride` (setters 0x475370/0x4753d0 char, 0x4a7cd5 state machine). Play API 0x474260(char, slot_sel, 0, entry, anm, char): slot_sel 0/2 -> `char+0x238`, 1 -> `char+0x1b0`, >=3 -> explicit slot pointer; it sets `slot+4 (base) = anm + 4` (callers pass 0x473b20(char) = `char+0x2dc` = the loaded `.anm` buffer, so base skips the u32 id header) and stores the script-entry pointer **directly** into `slot+0x40/+0x44` — the static templates (0x6c9928, 0x6c9940) are never copied or patched, and the earlier "who patches the base field 0x800" question is resolved: nobody does. Script entry = 12 B; the only fields the engine reads are `s16 +4` frame count (>=0 -> start frame = u16 +6, else u16 +8; play API 0x4742d4), `u16 +6` start, `u16 +8` end and `u8 +0xa` loop flag (state machine 0x4a7bd4: subframe advance, `frame = +6 + cur`, bound/loop against +8/+0xa). The `0x800` dwords in the 0x6c9940 template are the +4 frame-count field (2048), not a base placeholder.

Frame record (FULLY DECODED 2026-11-12; record walk verified frame-by-frame on all 92 files): self-describing compressed stream occupying `stride` bytes as the seek increment (the final frame of the two `test/` re-encodes is stored at natural length without padding). Two consumers share the grammar: 0x4a6d60 (writes translation slot bone+0x48, "current") and 0x4a6fc0 (symmetric, writes bone+0x98, "interpolation target"); both return the record end pointer (stored as next frameptr at 0x4a7baa). Grammar, read sequentially from `frameptr`:

- A u32 control word heads the record and repeats inline every 8 bones; bones consume nibbles from LSB upward, `type = nibble & 7` (nibble bit 3 is ignored — the player's 0x9/0xa nibbles are types 1/2).
- The bone loop walks the character's bone linked list (record +4 = next, +0 = parent pointer, 0 = root); bone count = `[modelblock+0xc]` = the paired `.mdl`'s nA, parent bytes = the first nA bytes at `.mdl` offB (accessors 0x4543b0/0x4543c0; binder 0x46a7f0 -> pool builder 0x4a6070, which sets bone+0 = 0 iff the parent byte is 0xff). Bone record = 0xd0 B, template identity matrix at 0x6b0980.
- type 0/3/4/7: no channel (0 B).
- type 1/5: rotation only, 6 B: 3 x s16 = quaternion vector part (x,y,z) x 32768 (scale constant 3.0517578125e-05 = 1/32768 at 0x68e8a0); `w = sqrt(max(0, 1 - |v|^2))` with sign +1.0 (type 1) or -1.0 (type 5), built at 0x4a6cd0, written to bone+0xb8..0xc4, then applied to the bone matrix via 0x4a6610.
- type 2/6: translation + rotation (rotation as above, sign + for 2 / - for 6). Translation: child bone (bone+0 != 0): 3 x IEEE-754 binary16 half-floats (decoder 0x4a6810), 6 B; root bone (bone+0 == 0, parent byte 0xff): 3 x float32, 12 B. Written to bone+0x48/4c/50 (+0x54 = 0, flag +0xc8 = 1 for half / 0 for raw; consumer 2 writes +0x98/9c/a0).
- Per-bone record size: 0, 6 (rotation) or 12/18 (translation+rotation; 18 only for root bones).

Walk verification: with the paired `.mdl`'s nA parent bytes, every frame of 56/92 files consumes exactly `stride`. The 31 player files (id 0x100) walk to exactly 0x108 per frame using the first 48 parent bytes of chhaa (bones 43..47 are type 0 in every frame, so the effective count is 43..48). The source of 48 is resolved (2026-11-13, runtime): there is **no separate 48-bone skeleton** — the player's runtime bone list is chhaa's full 73 bones, and 48 is just how many bone records fit in the 0x108-byte frame record; bones 48..72 stay in bind pose. Live proof (sh3-nx oracle): the binder is called with the player char (id 0x100, char=0x8984e0) and chhaa (nA=73); `char+0x238` is 0 before the bind and holds a 73-node list after it (head 0xb7f748, mirrored to `+0x1b0`), attached via 0x4a6920(&char+0x238, head). The earlier "shared from a template char" hypothesis (2026-11-12) is NOT exercised: fn 0x473fb0 (containing the copy at 0x47404c, reading `[template+0x238]` from `0x46a8f0(player_id - 0x80)`) is never called in the observed window, and the bone-pool builder 0x4a6070 is only ever called with nA=73/3/2/1 — no 48-bone pool is ever built. The kind-1 player branch at 0x59ceea is not a bone source either: 0x461d90 is a texture converter (ddCreateTexture + LockRect/UnlockRect, format byte at param+0xc: 0x10 = 4bpp half-width copy, 0x18/0x20 = byte copy; pixels at param+0x30+[param+0xd]), i.e. a cutout-texture fixup. The 3 `ded1` files (`bg_ded1`/`en_ded1`/`test_en_ded1`) have no paired `.mdl` in the archives; a DP walk (raw/half free) fits every frame with 3 bones (root type 2 + 2 rotation bones). The 2 `test/` re-encodes fit with an unpadded final frame.

### `.cld` collision (SOLVED 2026-11-11, tool `tools/j3_cld.py`, parses all 288 files)

World-space collision for one room, stored as a 4x4 grid of 5000-unit cells (exe: cell size 5000.0 at VA 0x690028, extent 20000.0 at 0x68f130; query helpers 0x4afb80 / 0x4ad320 scan the 16 loaded `.cld` slots at VA 0x7122440, loader 0x4adec0):

```
+0x00 float x0, z0                 grid origin (world)
+0x08 u32 size[5]                  byte sizes of sections 0..4 (they add up to the file size exactly)
+0x1C u32 0
+0x20 u32 table[85]                table[0..79] = cell list pointers, CATEGORY-major: table[cat*16 + zi*4 + xi]
                                   (cat 0..3; verified on all 288 files), table[80..84] = absolute offsets of sections 0..4
lists:  u32 polygon indices terminated by 0xffffffff
polygon record (0x50 B):  u32 type (0x101 or 1), u32 nverts (4, or 0 = empty), u32 flags, u32 0, 4 x (x, y, z, 1.0) world vertices
cylinder record (0x30 B, section 4):  16 B zero | (x, y, z, 1.0) base | (0, -height, 0, radius)
```
Record 0 of each polygon section is an empty record. Sections 1 and 3 are (almost) all vertical quads (walls; section 3 = low ledges, flag 0x1000); sections 0 and 2 mix floors, ceilings, slopes and walls. Flag bits observed: 0x1, 0x2, 0x4, 0x20, 0x40, 0x80, 0x100, 0x200, 0x800, 0x1000, 0x4000, 0x8000, 0x20000 (meaning not decoded; 0x1 and 0x20 are the most common). Validation: all 288 files have consistent size/offset/list structure, and the collision geometry lines up with the map in world space (am11: collision bbox x[20000,24800] z[16400,19600] vs map x[20000,24450] z[16800,19400]).

### `.cam`, `.kg2`, `.lv` field notes (2026-11-11; partial, mostly exe/data-derived)

**`.cam`** (285 files, size multiple of 0x80): array of 0x80-byte camera-zone records, then (in some files) leftover ASCII/garbage records (e.g. am11.cam records 3-6 are text like `THIS IS ...`, so valid records end where the garbage begins; the exe fix-up 0x5a1350 stops when bit 0 of the flags byte at `record+0x84` (= next record +4) is set). Record layout seen in am11.cam (floats unless noted):
- `+0x00..0x17` three (x, z) world points = outer trigger triangle; `+0x18` 50.0; `+0x1c` -1600.0 (y / height);
- `+0x20..0x37` three (x, z) points = inner (shrunken) triangle; `+0x38/+0x3c` two floats (-305/-305, 0/-1200...);
- `+0x40` s16 (widened to 32 bit by the fix-up) type/id; `+0x44`,`+0x48`,`+0x4c` small ints (0/6, 0/3, ...);
- `+0x54` float with sentinel codes: 10001.0 -> -300, 10002.0 -> -375, 10003.0 -> -600 (camera distance-like); `+0x58` 10001/10002/10003 -> -300 / -150 / 150; `+0x60` (0 -> 448.0, 10002 -> 384.0) default; `+0x6c` int selector (0 or 2) deciding the default of `+0x70` (-500 / -125 for 10001/10002);
- `+0x78..0x7c` floats such as (-17.5, 17.5), (2000.0 ...) (limits). Exact camera semantics (position/target/FOV) not decoded; the sentinel values are "use default" markers resolved at load time.


**`.cam` record statistics (933 plausible records over 285 files, 2026-11-11)**: `+0x00..0x14` six floats = outer trigger triangle (x0,z0,x1,z1,x2,z2) in world units (values like -21250/-18750/19250); `+0x18` float 50.0 in 488 records (margin/near value); `+0x1c` float y (-1600 / -2000 / -1700, i.e. height of the zone); `+0x20..0x34` six floats = second, smaller triangle; `+0x38/+0x3c` two floats (0 or -500/-1200 / -1200 -1500 -2000: second y range); `+0x40` s16 flag (96% zero, 1/3 otherwise); `+0x44`, `+0x48`, `+0x4c`, `+0x50` small ints (ids/types: +0x44 mostly 0/6/16, +0x4c mostly 1 (725), 3, 2, +0x50 0/3/1); `+0x54` float 10001.0 (727 records, the "use default" sentinel) or 10000.0 (185) or a real value (e.g. -1250); `+0x58` = 0; `+0x5c` small int 0/1/2; `+0x60` 0 or float (550, 427...); `+0x6c` int selector (0/6/7); `+0x70` float 0 or -17.4515; `+0x74` float 1.0 (579) / 17.4515; `+0x78` float -17.4515 (579); `+0x7c` float 17.4515 (578) or 2000 (114): (-17.4515, 17.4515) look like symmetric angular limits (degrees) and 2000 a distance. Semantics beyond this (which triangle is the player trigger vs camera region, the meaning of the ids) are inferred, not proven; the exe camera module (0x4c0d10 init with default sentinel 10001.0, record chooser near 0x4c3ea0-0x4c5000) was not traced.

**`.kg2`** (280 files): begins like a model header: `+0x04` = 0x70 (offset to data), a 4x4 matrix at 0x30..0x6f whose last row is the world origin of the room (amf1: (-19773.3, -125.4, 18216.3, 1.0)), then groups of compact geometry: `u16 pairs (counts)` followed by signed 16-bit fixed-point vertices (positions) and normals as s16 (0x7fff/0x8000 = +-1). Loader stores the pointer per channel at [0x7f4c40+4*ch]; its only consumer (0x4877d0) allocates 0x78-byte entries (up to 4 objects, one 0x58-byte header each). Likely shadow-caster/prop meshes; vertex format and scale not pinned down.

**`.lv`** (4 files: `cu1e.lv` etc.): `u32 0 | u32 1 | ... | 4x4 matrix (0x2c..0x6b: identity with translation (-23041, -40.5, -60000))`, then small index lists (`0002 0004 0004 0006` ...) and s16 vertex data: same compact style as `.kg2`; consumer 0x4d1067 only.

### Compact geometry groups (`.kg2`, `.lv`; data-derived 2026-11-11)

A mesh group is a run of 16-byte slots: slot 0 = header `u16 nslots (incl. header) | u16 b | u16 c | u16 d` followed by four s16 `(cx, cy, cz, r)` (centre and radius of a bounding sphere, e.g. `1066 -874 4413 1812`); the group occupies `16 * nslots` bytes. The body is a stream of 8-byte items of 4 s16 each: positions `(x, y, z, 1)` in object-local units (1 unit = 1 world unit; e.g. a 2000-wide box spans `173..2173`) and normals `(nx, ny, nz, 0)` with `+-32767/-32768 = +-1.0`; normals come in pairs with positions (`[normal, position]` per vertex after the first position pair). Examples: amf1.kg2 groups at 0x70 (9 slots), 0x100 (8), 0x180 (8) are boxes (8 positions, axis-aligned normals). Validation: in amf1.kg2 the group walk is consistent for 24 groups from 0x70 to 0x900; bc11.kg2 shows 52 groups. The file container around the groups is NOT uniform: the first dwords differ (`w1` = 0x70 offset in amf1 but a small count such as 0x16/0x2/0x3/0xa in other files), so the entry/offset table and the 0x78-byte object entries the exe builds (0x4ccd90: per object a 4x4 matrix copied by 0x678070, a type id, and a world-origin offset) are not fully mapped; likely contents are box/convex shadow or collision-helper volumes. `.lv` uses the same slot style after its matrix.

**`.mdl` vertex stream (superseded 2026-11-12):** the 12-byte records are 6 x s16 = position + normal with normals at scale 4096 (the "second basis/blend vector" reading was wrong); see "`.mdl` (decoded 2026-11-12)" above for the full directory, region map and what remains inferred (J-stream attribute semantics, D/E record fields).

## Other formats (status from the morning session, unchanged)

- `.cam`/`.cld` are float tables. `.tbn2` (effects) uses the container with fmt 0x08.
- `.000` movies (7 files in `data/movie/`, 11.6-165 MB; exe table at 0x710c58-0x710c88 / 0x6b07ac: op, ed_b, ed_u, ed_n, henshin, huxx, muxx): **SOLVED 2026-11-13** — MPEG-2 program stream obfuscated with an MSVC-CRT LCG keystream. Tool `tools/j3_movie.py` (`--check` verifies pack syncs; decrypts to `.mpg`). Routine located statically: `FUN_00405680` (the movie DirectShow source-filter read, `ReadFile` site `0x4056b2`); verified end-to-end on all 7 files — every 2048-byte sector decrypts to a `00 00 01 BA` pack sync and the file tail ends with the pack end code `00 00 01 B9`.
  - Scheme: the file starts with a `0x8000`-byte block whose plaintext is all zeros (the exe scan skips leading zero words). Its ciphertext carries the seed, gathered as 4 fixed bytes `seed = f[0x2dde] | f[0x3d40]<<8 | f[0x2a5c]<<16 | f[0x55e2]<<24`. LCG `state = state*0x5d588b65 + 1` (mod 2^32 — the MSVC `rand()` multiplier). `base = LCG^8192(seed)` (the state after running over the padding). In the data region (from `0x8000`) the keystream is **periodic with period `0x1000` bytes**: at every 4096-byte chunk the state resets to `base`, then per 32-bit word `state = step(state); plain = cipher ^ state`. `FUN_00405680` also zeroes the 4 seed bytes in its decrypted block (padding only; the data region is untouched).
  - The earlier "exact cubic in sector number, 3rd difference 0x80000000" observation was an artifact of the periodic keystream interacting with the constant MPEG pack-header fields, not a true polynomial cipher.
  - Impact: unblocks J5 (FMV). The plaintext is standard MPEG-PS (Sofdec); the port needs an MPEG-PS/video decoder but no custom cipher. Note: sh3-nx does not implement the CRI Sofdec DirectShow filter (`CoCreateInstance(E436EBB3-524F): no such class`), so movies never play in that oracle — the decrypt was located statically and verified on the files, not by runtime capture.
- Audio: SOLVED for extraction + decode (2026-11-11), tool `tools/j3_audio.py`
  (`ls`/`x`/`wav`/`aix`). Inventory: `sound/sd.afs` = AFS, 378 entries: 362 plain
  PCM RIFF/WAVE (mostly 16-bit mono 22050 Hz; a few stereo/32 kHz/odd rates),
  12 AIXF (multi-stream ADX music, e.g. entry 366 = 5 stereo 48 kHz streams,
  1,824,489 samples), 4 raw ADX (entries 362-365). `sound/demo_afs/*.afs` (38
  AFS): 372 ADX + 77 `0x12345678` blocks + RIFF WAVs. Counts over all 39 AFS:
  724 wav, 372 adx, 77 x78, 12 aix, 0 unknown.
  - AFS: `'AFS\0' | u32 LE count | count x (u32 off, u32 size)`; entries 0x800-aligned.
  - ADX: `80 00 | u16 BE copyright_off (data at +4+off) | enc=3 | blk=18 | bits=4 | ch | u32 BE rate | u32 BE total_samples | u16 BE highpass=500 | ver 3/4 | flags 0 (no encryption)`;
    18-byte frames per channel, interleaved per 32-sample block; scale = BE u16 & 0x1FFF, +1;
    fixed predictor coefs from highpass (`c=(a-sqrt((a+b)(a-b)))/b`, a=sqrt2-cos(2*pi*fc/rate),
    b=sqrt2-1; coef1=floor(c*8192), coef2=floor(c*c*-4096)); sample = nibble*scale + ((c1*h1+c2*h2)>>12).
    Verified: all 372 ADX entries decode to continuous audio (lag-1 autocorrelation ~0.95-1.0, no clipping;
    the few low-correlation starts are near-silent segments). Loop info (version 4 header, +0x24: flag, start sample, start offset, end sample, end offset, u32 BE; version 3 at +0x18) is parsed by `adx_header`: 138 of the 372 ADX entries loop (typically start 0, end = total samples).
  - AIX: `'AIXF' | u32 BE hdr_size | ...`; chunks start at 8+hdr_size: `'AIXP' | u32 BE size | u8 stream | u8 nstreams-1 | u16 BE payload | u32 BE counter | payload`;
    counter 0xffffffff = per-stream ADX header (no frames), else frame data appended to that stream.
  - `0x12345678` block (77 in demo_afs, not audio): `magic | 1 | n | 0 | n x {offset, kind, size, 0}` with 0x800 padding (0xa7 fill);
    kind-1 payload = index/table data, kind-2 = 0x18-byte records (cutscene/demo animation data). Format NOT decoded; belongs to the cinematics data, not the sound path.
  - The exe's CRI codec zone (0x64c000-0x670000) is the runtime decoder of these ADX/AIX streams; the tool is a host-side reference for validating a native decoder (J5).

## Tooling notes

- `j3_arc.py _png_save` had a writer bug (color type 2 with 4-byte rows) — all
  PNGs produced before 2026-11-11 ~14:00 were unreadable; fixed, outputs
  regenerated.
- `cmd_tex` now decodes every fmt 0x20/0x18/0x08 sub of a container to
  `<archive>_<name>_<i>.png`; multi-sub containers no longer skipped.
- `walk_subs` walks by magic (sub_count unreliable), skips the fmt 0x08 palette
  block between subs, and accepts the 0x40-byte pic preamble.

### `.cam` decoded from the exe (2026-11-12; static analysis of 0x5a1350 and 0x4c0d10; parser `tools/j3_cam.py`)

Loader `cam(I,0..3,G)` 0x5e8f20 stores the file pointer at [0xbcc238+4*ch], calls the fix-up 0x5a1350 on it, and 0x5e8e70 compacts the non-null channel pointers into the NULL-terminated list at 0xbcc21c (pointer saved in 0x711a604). The camera module (0x4c3ea0 -> 0x4c0d10(ctx=0x711a600, list, margin, 0)) walks that list and builds one 0x148-byte entry per record at `ctx+0x1d4+n*0x148` (count at ctx+0x53d4).

- **Terminator (proven by data):** a record whose u32 at `+0x44` has bit 0 set ends the array (checked before each record, so the first record can be a terminator and the file contributes nothing). With this rule all 285 files give 934 valid records and exactly 4 trailing junk records in 261 files, 7 in 24 files (the ASCII junk records) - a constant, so the rule is right.
- **Fix-up 0x5a1350 (per record, r = record base):** `r+0x40` s16 widened to s32; `r+0x54` sentinel 10001/10002/10003.0 -> -300/-375/-600; `r+0x58` same sentinels -> -300/-150/+150 (always 0.0 in the shipped data); `r+0x60` 0.0 -> 448.0, 10002.0 -> 384.0; `r+0x6c` selects how `r+0x70` is patched: 0 -> 10001/10002 become -500/-125, 2 -> 10001 becomes -0.5236 rad (-30 deg). Other selectors (6,7,9,10,...) are untouched. Data: `r+0x54` is the 10001 sentinel in 727 records, 10000.0 in 186 (a real value, not a sentinel), else real values (-1250, -630 ...).
- **Zones:** two oriented rectangles per record. A = `r+0x00..0x14` (p0 x,z; p1 x,z; p2 x,z), B = `r+0x20..0x34`. The builder takes yaw from the edge p0->p1 (atan of dx,dz), builds a Y rotation (identity 0x6b0980 + 0x60c280), rotates the points (0x5a1500) and keeps the min/max box for each zone (entry `+0x18/+0x1c` and `+0x20/+0x24` for A, `+0x30/+0x34` and `+0x38/+0x3c` for B), the height range `r+0x18,r+0x1c` (entry `+0x40,+0x44`), `r+0x38,r+0x3c` (entry `+0x28,+0x2c`), and four 4x4 matrices (entry `+0x48/+0x88/+0xc8/+0x108`, rotations and their negated-translation forms). Entry `+0` = the record pointer. Zone membership is tested against the player position at `ctx+0x164`/`ctx+0x16c` with an extra margin (the init call passes 10001.0 so every record is included); the signed distance to the box is computed by 0x4c02d0 (flag `r+0x44 & 0x80` is passed in and clamps negative distances to 0).
- **Per-frame camera (RESOLVED 2026-11-13, `FUN_004c3ea0` + the `0x4c05c0` callback):** `0x4c3ea0` is the camera update. It reads the player world position (`0x472fe0`), sets the **look-at target = player position** (`0xba99d4..e0`), runs the zone solver `0x4c0d10`, then computes the **camera position** (`0xba99c4..d0`) as a third-person chase cam: default mode = `player + (cos(θ+π), sin(θ+π))·1500` horizontally with `y = player_y − 1000` (θ = player facing yaw, `0x60baa0/0x60bb60/0x60bb70` = angle-normalize/sin/cos); mode 8 (`[DAT_0711f9d8+0x6c]==8`) = orbit a fixed anchor (`DAT_0711f9d8+0x20/+0x24`) at `atan2(dz,dx) − 15°`, `dist = sqrt(dx²+dz²) − 200`, `y = player_y − 1200`. It then registers the exporter `FUN_004c05c0` via `0x4986c0(&LAB_004c05c0, 999999, 1)`; that callback (a leaf, not a Ghidra "procedure") just copies the camera position vec4 (`0xba99c4..d0`) and look-at vec4 (`0xba99d4..e0`) plus two flag dwords (=1) and a zero out to the caller's buffers. So the `.cam` records are **per-zone camera parameter overrides** (the sentinel-resolved `r+0x54/0x58/0x60/0x70` = distance/height/angle defaults) keyed by the two oriented trigger rectangles; the base chase-cam constants are 1500/1000 (default) or anchor-orbit (mode 8).
- **Still open:** the exact meaning of the ids at `r+0x48/0x4c/0x50/0x5c` and the selector values other than 0 and 2 (they gate which zone is active and which override set applies, but the id→behaviour table is not pinned).

### `.kg2` / `.lv` container SOLVED (2026-11-12; exe loop 0x4ccd90 + data; parser `tools/j3_kg2.py`)

This replaces the earlier "container not uniform / entry table not mapped" notes. The exe object loop (0x4ccd90, called from 0x4877d0 for each kg2 channel) reads:

- File header 0x10 B: `u16 id`, `u16 hi` (0 makes the exe derive it with 0x4cc540), **`u16 nobj` at +4**. (The "0x70 / 0x16 / 0x2 ..." dwords seen at +4 are this object count: amf1 has 112 objects.)
- Then `nobj` objects, each: 16 B header (`u32 0 | u32 index | ngroups<<16 | 0 | 0`; `ngroups` = hi16 of the u32 at +4), a second 16 B row, a **4x4 float matrix at object+0x20** (copied by 0x678070 into the 0x78-byte runtime entry; the old "matrix at 0x30" is object 0's), and the groups from object+0x60.
- A group is `nslots` 16-byte slots (the exe advances by `s16(hi16 of the u32 at group+4) * 16`). Slot 0: `u16 nslots | u16 hi | u16 type | u16 nslots`, and its last 8 bytes are the bounding sphere `(cx,cy,cz,r)` as 4 s16. Then 8-byte items of 4 s16: positions `(x,y,z,1)` and normals `(nx,ny,nz,0)` (+-32767 = +-1.0). A group whose u32 at +4 equals 3 is special-cased by the loop (time-of-day state at 0xbaca60).
- **Proof:** with this walk all 232 non-empty `.kg2` files (48 are zero bytes) parse exactly to their last byte, 9,095 objects in total. The 4 `.lv` files (cu1e, mrdf, mu12, mu22) use the same container.
- **Runtime registration (confirmed 2026-11-13, `FUN_004877d0`):** the consumer caps at **4 channels** (`DAT_0712df92 > 3` rejects), reads `nobj = s16 @ kg2+4`, allocates one **0x58-byte header** per channel into the global list `DAT_0712dfec[0..3]` plus a `nobj * 0x78` entry array at `header+0x54`, sets `header+0xc = 1.0`, then calls `0x4ccd90` to fill. Channel (`param_3`) 0 bumps `DAT_0712df86`, channel 3 bumps `DAT_0712df8c` and sets `DAT_0712df80`. So the 0x78-byte entries are per-object runtime records (matrix from object+0x20 + type + world origin), one array per loaded `.kg2` channel.
- **Runtime USE resolved (2026-11-13, `FUN_00489850` + `FUN_00489fb0` + `0x4cc900`/`0x4ccb30`/`0x4cd1b0`/`0x4d1550`):** the registered entries feed a **per-frame render/display queue** — not collision, not baked shadow geometry. Two per-frame passes:
  - **Update `0x489850`:** loads the 4 `.kg2` cache slots (`0x447210`/`0x447270`, same slot-accessor family as `.map`; channel 1 has a built-in fallback blob at `0x6b83b0` in mode 3) through `0x4877d0`; computes a plane-side value `dot(player − hdr.anchor[+0x20..28], hdr.dir[+0x30..38])`; for category `hdr+0x1e==0` calls `0x487e30` → `0x4cc900` (walks the hierarchy list at `hdr+0x50`: records `{s16 entry-idx, s16 nchild, children…}`, pushes them into queue `DAT_0712dffc` with flag 1, back-links `entry+0x68/+0x64`, sets `hdr+0x18=1` when a child of type 1/2/4/5/6 appears) + `0x4ccb30` (pushes `{hdr, type=2}` and one `{entry+0x64}` slot per entry into display queue `DAT_0712e004`, then applies the per-id exclusion table at `DAT_0712e2a2` — `s16 id` + ≤32 `s16` entry indices — zeroing disabled entries); category `hdr+0x1e==3` goes through `0x488000` → `0x4cca20` + the same `0x4ccb30`.
  - **Render prep `0x489fb0`:** refreshes per-category context blocks (`DAT_0712d060/d260/d660/d960/db60/dd60/ce60/e080`) via `0x4d1550` (copies world matrix + the two view matrices at `view+0x134/+0x174` + player pos + the plane-side flag); for category-3 channels places a **directional-light anchor** at `−dir×10000 + entry0.pos` with a scale-0.04 matrix (`0x4163a0`); a separate 16-slot table `DAT_0712dfac[0..15]` (category `s8 +0x35` ∈ {0,3,4}, `u16` id pair `+4/+6` resolved by `0x46a220`/`0x473600`/`0x46a050` = level-object lookup by (room,obj)) handles object-attached lights incl. a proximity push (`dist<2000` → raise by `533.33−0.2667·dist`, cap 400); consumes the per-frame scratch table `DAT_0712d460` (4 records × 66 B: `s16 id` + ≤16 `s16` entry indices + key `+0x20`; `0x4cd1b0` matches `id` against `hdr+2` and marks entries via `0x450bb0`, assert string `"can't get bg move matrix"` at `0x692718`), then clears it to −1 at frame end.
  - `0x4cd1b0` also transforms each entry position (`entry+0x40`) by the channel matrix and stores a clamped distance at `entry+0x50` (entry type = `s16 @ entry+0x60>>16`: type 3 → min 8000, type 4 → min 10000, else raw).
  - **Conclusion:** `.kg2` = the per-room **dynamic light/effect object layer** — positioned entries + directional channels + level-script enable/disable, queued to the renderer each frame. The old "shadow-caster meshes ('kage')" guess is superseded; the compact groups are per-object volumes attached to these entries.
- **Still open:** which render pass consumes the `DAT_0712e004`/`DAT_0712dffc` queue records (exact draw semantics), the group-type grammar (type = low16 of the u32 at +4: 6, 5, 7, 11, 15, 9 ... most common), and the `DAT_0712dfac` light-slot category semantics (0/3/4, types `0x100`/`0x8xx`).

### `.mdl` outer structure correction (2026-11-12)
A character `.mdl` (e.g. chhaa.mdl, 5.25 MB) is the model block (the u32 at +0x0c is the ABSOLUTE end offset of the model block = start of the `ffffffff` container; 0xc1b50 for chhaa) followed by a normal `ffffffff` texture container at that offset (4 subs for chhaa: 512x512, 512x512, 512x1024 ...). So only ~790 KB is geometry/skeleton; the rest is textures readable with `j3_arc.walk_subs`/`decode_container`. The `.mdl` directory is now decoded (see "`.mdl` (decoded 2026-11-12, J3 item b)" above; tool `tools/j3_mdl.py`); `.anm` is decoded (container: size formula + stride table; per-frame record: full channel grammar, walk verified on all 92 files — see "`.anm` character animation" above); the working brief is `docs/BRIEF-MDL-ANM.md`.

