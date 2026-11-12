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

Exe hunt status (open): marker `0x99990909` absent from the whole exe image in
both byte orders (never compared); no xref or stored pointer to the pic path
table at `.data 0x6d0cbc` (the only `?? 0c 6d 00` hit, 0x625eb7, is a data
table); `0x1030` immediates in `.text` are struct field offsets in the tile
compositor cluster (FUN_00635c45 etc.), not the palette size. Remaining
anchors: suffix table VA 0x6910a0 (FUN_0049bee0 manifest builder); `.arc`
magic immediate 0x608e61 (FUN_00608e10 ← FUN_006097f0 ← 0x6068f0); texture
cluster 0x631xxx–0x63axx (DXT dispatch FUN_00639d33, region ctor FUN_00636081).

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

### Texture page rule (exe-derived 2026-11-11, visually confirmed on am11)

The wrapper `k` (type `0x1000k`) is **not** a page index. The exe draws each
TOP-LEVEL section with one texture (`SetTexture(0, ...)` once per section in
FUN 0x4590e0 / 0x459150 / 0x4591c0, then FUN 0x458c30 draws the whole section);
all wrappers and leaves under it share that texture. The leaf `mat` (grid cell)
only feeds vertex-shader constants; `k` and the type-0/4 intermediate type only
select a PS2 packet record and a render-mode table (0x6b4670), never a page.

| list (header) | count word | top type | texture |
|---|---|---|---|
| L1 `+0x1C` | `+0x42` | 1 | `<fam>GB.tex` sub index = section `dw5` (u32 at section+0x14, signed; -1 = last sub, i.e. `count + dw5`) |
| L3 `+0x20` | `+0x46` | 2 | `<room>TR.tex` page i (i = index in list; list has 0 or 1 entries) |
| L2 `+0x24` | `+0x44` | 3 | inner container page i (i = index in list) |

Facts backing this (all 287 maps): L2 count == inner sub count in every room;
L1 `dw5` is always < GB sub count (families with 7 GB subs use -1 and 0..5, `cc`
has 4 subs and uses 0..2, `hp` has 2 and uses -1/0); L3 count is 0 or 1. Exe
side: GB set = global `tex(G)` (0x867d00, loader callback 0x5e8f70), TR set =
`tex(T)` (0x867530, callback 0x5e8fb0); index setup FUN 0x44f1b0 (reads
`[section+0x14]`, negative => `0x45be20() + dw5`). Type-3 `dw5` is a running
number (e.g. 2..8) and is NOT used on the PC path; page = list index.
Rendering am11 with this rule gives a coherent room (wood floor, tiled
bathroom; bcf1 ducts/beams continuous); the old "k -> GB page" guess does not. TR coverage: L3=1 with own `<room>TR.tex` 66 rooms, L3=0 199 rooms, L3=1 without own TR 22 rooms — all in family `cc`, whose archive has one TR file (`cc01TR.tex`), so those rooms share it (global `tex(T)` set; exe load order not traced). Visually confirmed on am11 and bcf1 (multi-section, dw5 0,1,2,3,5).
The earlier dark-purple `cc` GB
pages were the fmt 0x10 green-channel bug (fixed, see fmt 0x10), not the rule.
Reference renderer: `sh3-vita/build/map-scratch/render_exe_rule.py <room> exe`.

Open: UV space check on 512x1024 GB subs (cc idx 3 is never referenced);
lighting (vertex color may carry it); fmt 0x04 (see below, parked); the 0x49xxxx cluster is a file registry; the geometry draw/texture path is
located (see Texture page rule), the file loader itself is not.

### Leaf outliers EXPLAINED (2026-11-11)

Re-walk of all 287 maps through the three header lists: 33,739 leaves, 478 do not satisfy `size == 0x40 + n*0x24`. They are all in rooms `cuf3`, `mu12`, `mu22`, `mu2d`, `mu2f`, `mu32`, `muee`, `muf1` (plus a few more mu*). 464 sit under a type-**10** intermediate, 4 under type 9 (cuf3), 10 under type 0 (mu2d, muee). Each still starts with `n` normal 0x24-byte vertices; after them comes an auxiliary block: leaf header `dw8` (type 0/9 leaves) or `dw9` (type 10 leaves) = `16*(n+4)` bytes of per-vertex auxiliary data, and the total extra after the vertices is `4*n` (+0..28 alignment padding) bytes for type 10 (words like `0x80131414`, `0x80139314`: packed per-vertex values, probably a second colour/light term) or larger blocks for type 0/9. The auxiliary data is not geometry: a renderer can read `n` vertices from the leaf and ignore the rest.
Script: `sh3-vita/build/map-scratch/outl.py`.

### fmt 0x04 (parked)
Only `pic/test.tex`, `pic/mp/mpmchurch1_10_tr.tex`, `mpmchurch2_10_tr.tex` (512x1024). Not DXT1 and not linear: PS2-style 4bpp indexed pixels (262144 bytes at sub+0x30) with a 16-entry RGBA palette (alpha 0x80 = opaque) located in the bytes after the pixels (first entries `00000080 09090980 1b191980 ...`). The pixel order is PS2 GS-swizzled (PSMT4): nibble-linear decode shows the tile/text structure but scrambled. These look like unused PS2 leftovers; unswizzle not implemented.

### `.cam` / `.cld` / `.ded` / `.kg2` survey (sizes only, 2026-11-11)
- `.cam` (285 files, 512-3328 B): size is a multiple of 128 in all files => fixed 0x80-byte records; each starts with 3 float pairs + 3 float pairs (XZ points of a trigger/camera area), then 50.0, -1600.0 and small ints; semantics not decoded.
- `.cld` (288 files): SOLVED, see "`.cld` collision" below.
- `.ded` (285 files): SOLVED, see "`.ded` - per-room light list" above.
- `.kg2` (280 files, 0-123616 B): loader 0x5e8d20 stores the pointer per channel at [0x7f4c40+4*ch] (flag [0x7f4c08]); the only consumer (0x4877d0, called from 0x489800) registers up to 4 objects with 0x78-byte entries. Content looks like compact geometry: 16-bit fixed-point positions and +-1.0 normals as s16 (0x7fff/0x8000) with small vertex counts (6, 8 ...), i.e. shadow/prop meshes ("kage"), NOT lights. Not decoded.
- `.lv` (4 files): loader 0x5e8d90, pointer at [0x7f4c7c+4*ch] read only by 0x4d1067; starts with a 4x4 matrix and small index/vertex data; not decoded.
- Counts over all arcs: tex 383, mes 368, cld 288, map 287, cam 285, ded 285, kg2 280, mdl 157, anm 92, sdb 81, kg1 69, tbn2 34, pic 32 (+hd/bd/td 32 each).

### `.mes` message files (partly decoded 2026-11-11, tool `tools/j3_mes.py`)

`msg.arc`: 368 files = 46 areas x 8 languages (`_eng_ _jpn_ _frn_ _grm_ _itl_ _spn_ _krn_ _chn_ _msg.mes`). Layout: a u16-LE table T0 (n = T0[0]/2 monotonic offsets, T0[0] = its byte size) pointing at group records of u16-LE text pointers into the text area. English text is readable: `0x00` = space, `0x07` = apostrophe, `0x0e` = line break, `0x29` = `I`, `0x80` = sentence-start prefix (the next letter is stored 0x20 low: `<80>,OOKING` = "LOOKING"), `0x90`/`0x10`/`0x0c`/`0xff xx` = style, page and argument escapes (`0xff 0x03` before item names), strings contain no lowercase. The exact escape grammar and the Japanese/Korean/Chinese encodings are not decoded; the group record structure (which pointer belongs to which message id) is only inferred.

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

### `.mdl` / `.anm` (open): `data/pcchr/<group>/<name>.mdl` (157 files, chr*.arc) is NOT the `.map` section tree. Header: `u32 0 | u32 id (e.g. 0x1027) | u32 part count (1-3) | u32 size | u32 size | u32 0x50 | 0 | 1 ...`, a part table at 0x40 (offset/size pairs), then at 0x50 a different small header (`ffff0003 3 0x80 1 0xc0 ...`) followed by what looks like 16-bit fixed-point vertex data (6 x s16 per vertex, e.g. `fec1 fff2 05b9 ffd0 0239 0ec5`) like `.kg2`; skinned draws use the VS with weights `v1`, bone indices `v2`, normals `v3`, uv `v4` (VS at 0x6b57b8, bone matrices from c48 with index*4). The model loader and animation (`.anm`, 92 files, e.g. `data/chr/en/*.anm`) were not analysed; model paths are in static tables starting at VA 0x6954d5 (`data/pcchr/pl/rchfuda.mdl` ...).

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

**`.mdl` vertex stream (observation, bg_bld.mdl, 2026-11-11):** after the part table, the 0x50 pre-header (`ffff0003 | 3 | 0x80 | ...`) and node/matrix records (bg_hum.mdl shows 4x4 matrices with rotation + translation, e.g. rows `(0, -0.96, 0.28, 0)`, `(.., -716.0, -16.56, 1.0)` = a skeleton/node hierarchy), the geometry at 0x140 is a stream of **12-byte vertices = 6 x s16** (1730 records between 0x140 and 0x5260 if contiguous). In bg_bld the records come in groups of 4 forming quads: `(-14,-319,-48,1465,3781,569) (14,-318,-48,-1294,3842,579) (-14,-322,0,1465,3824,0) (14,-322,0,-1294,3886,0)`: components 0,1,3 behave like a position (+-14, ~-320, +-1294..1465 = a thin plane), components 2,4,5 form a second 3-vector whose length equals the next quad vertex's component 4 (sqrt(48^2+3781^2+569^2) = 3824 = vertex 2's component 4), i.e. each vertex carries two vectors (position and a second basis/blend vector, possibly bone-relative positions or normal/tangent scaled by ~3824-3886). The index/strip structure, the scale, the node table layout and the skinning weights were NOT decoded; `.anm` untouched.

## Other formats (status from the morning session, unchanged)

- `.cam`/`.cld` are float tables. `.tbn2` (effects) uses the container with fmt 0x08.
- `.000` movies (7 files in `data/movie/`, 11.6-165 MB; exe table at 0x710c58-0x710c88 / 0x6b07ac: op, ed_b, ed_u, ed_n, henshin, huxx, muxx): **scrambled, NOT decoded** (2026-11-11 partial analysis).
  - Size = 4 mod 2048 for all 7 (N 2048-byte sectors + 4 bytes); entropy 7.999; first and last dword share their top byte (huxx b4.., muxx 75.., op e9.., ed_b/henshin 8c..); both are sector-start words (the trailing 4 bytes are word 0 of sector N).
  - Structure found: for each 32-bit word position j in the first ~24 words of a sector (the part that is MPEG-PS pack/system header in plaintext), the ciphertext word as a function of the sector number s is an EXACT cubic polynomial mod 2^32: 3rd finite difference over s is the constant 0x80000000 for every j, 4th is 0; 1st/2nd differences carry 9/20 trailing zero bits; byte 0 of each word is constant across sectors. Beyond the first ~24 words the content looks random (payload). So the cipher is a per-(sector, word) keystream with polynomial structure; plaintext is probably Sofdec/MPEG-PS 2048-byte sectors (starts 00 00 01 BA).
  - Not a repeating XOR key (column dominance only in the first 23 bytes), not a plain affine LCG over sectors (no (A,C) reproduces word 0 across 59 sectors).
  - The decrypt routine in the exe is NOT located: candidates ruled out so far = FP-control code at 0x670540, movie wrapper/state machine 0x59c9c0-0x59cc60 and 0x5f1b50-0x5f1bd0 (path/play only), CreateFile/ReadFile sites at 0x4056b2, 0x609055, 0x61cb27 (generic CRI file read, no decrypt seen). Next: trace the file-read callback registered for the Sofdec/MPEG stream (the sector-sized reader reached from the CRI codec zone 0x64c000-0x670000 / 0x66f7af ReadFile), or break the cipher by deriving the keystream formula from the polynomial coefficients (per-word coefficients are readable from the first sectors).
  - Impact: needed for J5 (FMV). Without it the movie path cannot be validated; HANDOFF lists it as the open risk.
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
- **Still open:** the meaning of the ids at `r+0x48/0x4c/0x50/0x5c`, of the selector values other than 0 and 2, and what `0x4c05c0` (callback passed to 0x4986c0 at the end of 0x4c3ea0) does per frame with the camera position/angles stored at 0xba99c4..0xba99e0.
