# Changelog

## 0.0.1 — development snapshot (2026-11-13)

First tagged state of the Silent Hill 3 port, spun off from the SH4 Vita port
(sh4vita). **Host-tested only: PC (Docker, Mesa software GL, dummy audio) and the
Vita3K+ emulator. Nothing has been confirmed on a real PS Vita.**

- Recompiler `--target sh3`, SIMD outside the CRI codec translated (J0/J1).
- Linked startup on the PC host: logos, content warning, title menu, New Game,
  in-engine intro and the first playable room (`tpf1`) with walking and wall
  collision (J2). Spotlight cube maps and wrapping stencil ops follow D3D8
  behaviour.
- Data formats documented and re-checked by `tools/j3_check_all.py` (J3).
- ADX/AIX/AFS decoders and the `.000` movie demangler, bit-exact on the host (J5
  groundwork; only the demangler is used by the runtime).
- Options → Configuration → **Textures** (Performance / Balanced / High, saved in
  `sh3port.ini`); Options → Resolution lists real modes.
- Credits page before the title screen and the version in the title menu's
  bottom-left corner, taken from `VERSION` (VPK `APP_VER` 00.01).
- Vita build: VPK title ID `SILH00003`, data in `ux0:data/sh3/`. Boots in
  Vita3K+ but showed a black screen at the last check; a Vita file-read-offset
  fix and texture-upload/timer churn reductions are in the source, not re-run.
- Release documentation: README, `THIRD_PARTY_NOTICES.md`, `LICENSES/`. Known
  gaps are listed in the notices (missing zlib/newlib/libgcc/taiHEN texts,
  LiveArea artwork origin, SceShaccCgExt GPL question).

Not in this snapshot's commits: the in-game opening movie (`op.000`), which plays
on the PC build in an uncommitted working-tree change.
