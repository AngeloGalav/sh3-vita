# sh3-port — Silent Hill 3 (2003 PC) to PS Vita

Version **0.0.1** (see `VERSION`). An experimental static-recompilation port of
the 2003 `sh3.exe` (x86 PE32) to the PS Vita. The recompiler translates the
game's own x86 code to C; the host provides a Win32 / D3D8 / DInput /
DirectSound shim and vitaGL rendering.

> **Read this first.** Nothing in this repository has been confirmed on a real
> PS Vita. All testing so far is on a PC (Linux, Docker, software GL) and in the
> Vita3K+ emulator. Emulator results are not console results. Version 0.0.1 is a
> development snapshot, not a playable release.

This project does **not** contain or distribute any Silent Hill 3 files. You
need your own copy of the 2003 PC release.

## What works (0.0.1)

Host-tested only (PC, Docker image `sh3host:local`, Mesa software GL, dummy
audio driver):

- Boot, Konami logo, content warning, the port's credits page, title menu.
- **New Game**: the in-engine intro, then the **first playable room** (`tpf1`,
  Lakeside Amusement Park entrance). Heather walks (arrow keys / WASD) and stops
  at walls. In the views compared so far the room renders like
  [sh3-nx](https://github.com/cutarev/sh3-nx), the independent port of the same
  exe used as rendering reference (evidence, not ground truth).
- Options → Configuration → Textures (see below).
- Opening movie `op.000` plays in-game on the PC build through the game's own
  DirectShow-style graph. Only the first ~90 s of 192 s were checked; the end of
  the movie, skipping, the other six movies and audible audio are untested. The
  source change that does this was still uncommitted when this README was
  written.

What is **not** confirmed:

- **Anything on a physical PS Vita.** No console run has happened.
- **Vita3K+**: the VPK boots, loads the exe and archives and reaches the game
  loop, but showed a black screen at the last check. A fix for a Vita-side file
  read offset is in the source and has not been re-run on the emulator.
  Emulator results are emulator-only.
- Real audio output (the runtime audio path was only run on a dummy driver),
  performance, memory headroom and texture VRAM cost on a Vita, save/load round
  trips, anything past the first room.

## Installing on the Vita

This is for people who can build the port themselves: 0.0.1 ships no pre-built
VPK, and none of the steps below has been verified on a console.

1. Build the VPK with `docker/build-vita.sh`. It needs the generated code in
   `port/gen-sh3/`, produced by `python3 recomp/x86rec.py --target sh3` from your
   own `game/sh3.exe`. The output is `sh4.vpk` (the file name is inherited from
   SH4); title ID **`SILH00003`**.
2. Install the VPK with VitaShell.
3. Copy **your own** Silent Hill 3 PC files to `ux0:data/sh3/`: `sh3.exe` and the
   `data` folder from your installation. The supported executable is the 2003
   release with SHA-256
   `8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918`; other
   builds are not supported. If `sh3.exe` or `data` is missing, the port shows a
   "GAME FILES NOT FOUND" screen.
4. Like any vitaGL program, the port compiles shaders at run time and needs the
   Vita's `libshacccg.suprx` to be installed already. This project neither
   supplies nor links to it.

Game files are your responsibility: this project provides no download of
`sh3.exe`, game data, or any proprietary Sony module.

Logs go to `ux0:data/sh3/sh4log.txt` (the log name is inherited from SH4).

## Options

**Options → Configuration → Textures**, reached from the title menu, selects the
texture storage quality. It replaces the PC "Screen Mode" row in the Western
menus (the CJK menus keep the original label).

| Setting | Meaning |
|---|---|
| Performance | Smallest storage: large A8R8G8B8 textures stored as RGBA4444 (visible colour speckle). |
| **Balanced** (default) | RGB565 only for fully opaque textures, RGBA8 otherwise. |
| High | RGBA8 everywhere. |

The choice is applied when you confirm and is saved as `textures=0|1|2` in
`sh3port.ini` in the game folder (`ux0:data/sh3/` on the Vita). It has been
looked at on the PC with the Vita texture path emulated; its VRAM and speed cost
on a Vita is not measured.

Options → Resolution lists 640×480, 800×600 and 1024×768 (the original list
failed to enumerate).

## Debug environment switches

For development. They are read with `getenv`. On the Vita, put one `KEY=VALUE`
line per variable in `ux0:data/sh3/env.txt` (read at startup, before vitaGL
initialises). On the PC, export them as ordinary environment variables. Defaults
are what the code does with the variable unset.

| Variable | Effect |
|---|---|
| `SH3_NO_MOVIE=1` | Movies unavailable: the movie graph cannot be created and the game moves on. For scripted runs. |
| `SH3_NO_CREDITS=1` | Skips the port's credits page before the title screen (scripted runs). |
| `SH3_TEXSUB=0` | Goes back to a full `glTexImage2D` for every texture upload. Default for SH3: in-place `glTexSubImage2D` when storage of the same size and format exists. |
| `SH3_CUBE=0` | Binds the spotlight cube-map stage as NULL, as before 2026-11-13: the warm cone of light on walls and characters disappears. Default: cube maps bound. |
| `SH3_TEXQ=0\|1\|2` | Forces the texture quality (Performance / Balanced / High) over `sh3port.ini`. |
| `SH3_TEXFREE=1` | Opt-in: frees guest-side texture copies after upload (PC: used guest memory 67.0 → 46.0 MB in the first room). Off by default; effect on a Vita not measured. |

PC-only: `SH3_GAME_DIR` (game folder), `SH3_SAVE_DIR` (write overlay so the game
folder stays read-only), `SH3_SHOTS=f1,f2` (PPM screenshots of the back
buffer), `SH3_TRACE`, `SH3_SYNCLOG`, `SH3_FILELOG`, `SH3_READLOG`,
`SH3_TRACE_SHADER`, `SH3_MEMCRC`, and `SH4_INPUT="frame:DIK[:duration],..."` for
scripted keyboard input.

## Credits

**AI-assisted port by [AngeloGalav](https://github.com/AngeloGalav).** Based on
the work of (the same entries as the in-game credits page,
`port/src/host/sh3_port_ui.c`):

- vallfrr's [sh4vita](https://github.com/vallfrr/sh4vita)
- cutarev's [sh3-nx](https://github.com/cutarev/sh3-nx)
- AngeloGalav's [sh2-vita](https://github.com/AngeloGalav/sh2-vita)
- Silent Hill 3 PC Fix by Steam006 (2.8.9.1),
  <https://community.pcgamingwiki.com/files/file/1331>

Third-party software compiled into the port is listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), with license texts in
[`LICENSES/`](LICENSES/). Silent Hill 3 and its assets belong to their owners
and are not part of this project.

## Layout

- `recomp/x86rec.py` — x86→C static recompiler. `--target sh3` reads
  `game/sh3.exe` + `re/out/functions-sh3.json` and writes `port/gen-sh3/`.
- `re/` — Ghidra export script (`ghidra/ExportFunctions.java`), PE import
  lister (`pe_imports.py`), SH4 import ledger (`LEDGER.md`, *inherited from SH4*).
- `port/` — Vita/PC host: Win32+DX shim (`src/win32`, `src/dx`, `src/d3d8`),
  runtime (`src/rt`), platform layers (`src/vita`, `src/pc`, `src/host`), vitaGL
  patches (`patches/`, *inherited from SH4*).
- `docker/` — VitaSDK image, `build-vita.sh`, `smoke.sh` (toolchain check, no
  game code), `host-build.sh` (PC test image).
- `docs/` — study and tracking documents (English).

*Inherited from SH4 and not renamed:* the CMake target and VPK file name
(`sh4`, `sh4.vpk`), the `SH4_*` environment variables and compile options, and
the log name `sh4log.txt`. `-DSH3=OFF` builds the inherited SH4 layout, which is
not maintained here.

## Inputs (never versioned)

- `game/sh3.exe` — oracle: 2003 release, SHA-256
  `8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918`.
  No Steam/GOG release of SH3 exists; this binary is the sole reference.
- `re/out/functions-sh3.json` — Ghidra export (8,145 functions, 532,994 body
  instructions).
- `port/gen-sh3/` — generated C, never committed.

## Status by milestone

- **J0** (target tooling): see `docs/PORTAGE-SH3-2026-11-11.md` §9–§11.
- **J1** (SIMD outside the CRI codec): done, differential test green.
- **J2** (linked startup → first room): works on the Linux host as described
  above.
- **J3** (data formats): complete, documented in `docs/DATA-FORMATS-SH3.md`;
  `tools/j3_check_all.py` re-checks every format against the documented counts.
- **J4** (D3D8 on Vita): census done (`docs/J4-D3D8-CENSUS.md`); translator not
  started.
- **J5** (native decoders): ADX/AIX/AFS decoders and the `.000` demangler in
  `port/src/codec/` are bit-exact against the J3 tools on the host. The runtime
  uses only the `.000` demangler (movie path); the audio decoders are not wired
  in.
- **Vita build**: `docker/build-vita.sh` produces an SH3 VPK (see above). Not run
  on a console.

Testing is on PC and the Vita3K+ emulator; emulator results are never console
validation.

## How this was made

*Inherited from the SH4 README and adapted.* This is an experimental project,
built on top of the [Silent Hill 4 Vita port (sh4vita)](https://github.com/vallfrr/sh4vita)
by vallfrr: recompiler approach, Win32/D3D8 shim, runtime. Most of the work was
done with **Qwen Flash Next**, a local model, and with **Claude Sonnet**;
validation with **Claude Opus** is planned for later. Treat every claim in the
docs as "checked on PC/emulator only" unless stated otherwise.
