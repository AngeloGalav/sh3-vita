# sh3-port — Silent Hill 3 (2003 PC) to PS Vita

Static-recompilation port of the 2003 `sh3.exe` (x86 PE32) to the PS Vita,
spun off from the qualified SH4 Vita port (`../sh3-vita`). The recompiler
translates the game's own x86 code to C; the Vita host provides the Win32 /
D3D8 / DInput / DirectSound shim and vitaGL rendering.

## Layout

- `recomp/x86rec.py` — x86→C static recompiler. `--target sh3` reads
  `game/sh3.exe` + `re/out/functions-sh3.json` and writes `port/gen-sh3/`.
- `re/` — Ghidra export script (`ghidra/ExportFunctions.java`), PE import
  lister (`pe_imports.py`), SH4 import ledger (`LEDGER.md`).
- `port/` — Vita/PC host: Win32+DX shim (`src/win32`, `src/dx`, `src/d3d8`),
  runtime (`src/rt`), platform layers (`src/vita`, `src/pc`), vitaGL patches.
- `docker/` — VitaSDK image + `smoke.sh` toolchain validation (no game code).
- `docs/` — study and tracking documents (English).

## Inputs (never versioned)

- `game/sh3.exe` — oracle: 2003 release, SHA-256
  `8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918`.
  No Steam/GOG release of SH3 exists; this binary is the sole reference.
- `re/out/functions-sh3.json` — Ghidra export (8,145 functions, 532,994 body
  instructions).

## Status

J0 (target tooling): see `docs/PORTAGE-SH3-2026-11-11.md` §9–§11.
Milestones: J0 tooling → J1 SIMD outside the CRI codec → J2 linked startup;
J3 (data formats) in parallel. Testing is on PC and the Vita3K+ emulator;
emulator results are never console validation. A physical Vita may come later.
