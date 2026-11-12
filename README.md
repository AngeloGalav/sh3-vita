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

- **J0** (target tooling): see `docs/PORTAGE-SH3-2026-11-11.md` §9–§11.
- **J1** (SIMD outside the CRI codec): done, differential test green.
- **J2** (linked startup): reached on Linux. The recompiled `sh3.exe` runs in a
  Docker image (SDL2, Mesa software GL) with the real game data and shows the
  Konami logo, the content warning and the title menu. The opening movie is
  skipped (`SH3_NO_MOVIE=1`), audio is untested, and nothing past the menu has
  been exercised. Build and run: `docker/host-build.sh`, `port/Makefile.linux`.
- **J3** (data formats): `.arc`, textures, `.map`, `.ded`, `.cld`, audio
  documented in `docs/DATA-FORMATS-SH3.md`; `.mdl`/`.anm`, camera semantics and
  the movie cipher are still open.

Testing is on PC and the Vita3K+ emulator; emulator results are never console
validation. No physical Vita or console validation has happened yet.

## How this was made

This is an experimental project, built on top of the SH4 Vita port
(`sh3-vita`'s SH4 code: recompiler approach, Win32/D3D8 shim, runtime).
Most of the work was done with **Qwen Flash Next**, a local model. The results
were then validated with **Claude Sonnet**; validation with **Claude Opus** is
planned for later. Treat every claim in the docs as "checked on PC/emulator
only" unless stated otherwise.
