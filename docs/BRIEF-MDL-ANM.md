# Brief: decode `.mdl` and `.anm` (J3 item b)

Audience: a fresh session (Qwen Flash Next, local). English only. Nothing here is executed on a console.
Repos: `/home/agala-ai-linux/projects/sh3-port` (code, docs, tools) and `/home/agala-ai-linux/projects/sh3-vita` (game data in `Silent Hill 3/data`, scratch under `build/`, not versioned).
Read first: `docs/DATA-FORMATS-SH3.md` (sections `.mdl` / `.anm`, "Compact geometry groups", `.kg2` container, `.cam`) and `tools/j3_arc.py`, `tools/j3_kg2.py`, `tools/j3_cam.py` for the style that worked.

## What is already known (verified 2026-11-12)
- Extract: `python3 tools/j3_arc.py extract "<sh3-vita>/Silent Hill 3/data" chrpl 51 -o OUTDIR` gives `chhaa.mdl` (5,250,384 B). `ls` lists archive entries; `.mdl` live in `chr*.arc` under `data/pcchr/<group>/`, `.anm` under `data/chr/<group>/` (157 mdl, 92 anm in total).
- `.mdl` outer layout (chhaa.mdl): `u32 0 | u32 0x100 (id) | u32 4 (parts) | u32 0xc1b50 | u32 0xc1b50 | u32 0x60 | 0 | 1`, part table at 0x40 (4 x offset 0x60, 4 x size 0xc1b50), the model block starts at 0x60 with `ffff0003 | 3 | 0x80 | ...`.
- **The model data is only 0xc1b50 bytes (~790 KB).** At file offset 0xc1b50 a standard `ffffffff` texture container starts (`j3_arc.walk_subs(b[0xc1b50:])` finds 4 subs: 512x512 fmt 0x18, 512x512 fmt 0x18, 512x1024 fmt 0x18, ...). So the rest of the 5.2 MB file is textures that the existing reader already understands; do not treat them as geometry.
- Inside the model block: a directory at 0x60 (`ffff0003`, count 3, offsets such as 0x80, 0x12c0, 0x13d0, 0x17850, 0xb9120 ... sizes such as 0x49, 0x5b, 0x37), then 4x4 float matrices from ~0xe0 on (a node/bone hierarchy: rotation rows plus translation such as (-716, -16.5, 1.0)), then a stream that looks like 12-byte vertices of 6 x s16 (position-like + a second 3-vector).
- Runtime facts from the exe: skinned draws use the vertex shader at VA 0x6b57b8 with inputs v1 = blend weights, v2 = bone indices (multiplied by 4 for the constant index), v3 = normal, v4 = uv, bone matrices from constant c48. Shader dispatcher: 0x463400 (called from 0x45ed40, 0x4628d0, 0x463640); the draw module is around 0x45d340-0x45f5d0 and 0x462240-0x463a50. The `.map` model header (object init 0x44f790) is NOT the `.mdl` header.
- `.anm` (e.g. `pl_htr_a.anm`, 465,436 B for the player; many files have the same size): first dword 0x100, then data that looks like floats/s16 mixed. Not analysed.
- Other chr files in the same archives: `.kg1` (chr shadow/prop geometry, 12-32 KB), `*_tex.dat` (textures, ffffffff container).

## Tasks, in order (each ends with a measurable check)
1. **Directory and parts.** Walk the `ffff0003` directory of every `.mdl` (157 files). Write `tools/j3_mdl.py` that parses it and asserts that every file's model block ends exactly at the offset where the `ffffffff` container starts (part table size field). Report counts of directory shapes (how many files share the same directory layout).
2. **Records.** For one small non-player `.mdl` (use `ls` to pick the smallest) identify each directory entry's content: matrices (node table), vertex stream, index/strip stream, per-mesh headers. Use byte statistics, alignment, and value ranges. Acceptance: every byte of the model block is assigned to a named region, no gaps.
3. **Vertices.** Decide the vertex layout (6 x s16 candidate). Check against the exe: skin weights, bone indices, normals, uv. Validate by rendering a point cloud (PNG, matplotlib or PIL) of one character in a bind pose and checking it looks like a body.
4. **Textures.** Use `j3_arc.decode_container` on the embedded container and save the PNGs; match texture index to mesh if there is a per-mesh texture field.
5. **`.anm`.** Same method: header, per-bone channels, frame count (guess from size / (bones x channel size)). Acceptance: a size formula that holds for all 92 files.
6. Update `docs/DATA-FORMATS-SH3.md` (replace the ".mdl / .anm (open)" paragraphs) and `docs/HANDOFF.md` in sh3-vita with the verified layout and what is still guessed. Mark every claim "verified on N files" or "inferred".

## Rules
- Read the exe (`game/sh3.exe`, SHA-256 8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918) with python `capstone` + `pefile` (see the helper pattern: image base 0x400000, `.text` 0x401000, `functions` list in `re/out/functions-sh3.json`). Do not guess a field from data alone when the exe loop can be read.
- Do not claim anything works on Vita or console. No subagents, no publishing, no pushes.
- Commits (only if the user asks): author and committer `Angelo Galavotti <agalavottib@gmail.com>`; message in Heather Mason's voice (Silent Hill 3) with all technical facts intact; trailers `Co-Authored-By: Heather Mason <heather@silent-hill.invalid>` and `Co-Authored-By: Qwen Flash Next <noreply@qwen.ai>`. See `AGENTS.md`.
