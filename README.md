<h1 align="center">sh3-port: Silent Hill 3 for PlayStation Vita</h1>

<p align="center">
  An experimental static-recompilation port of the 2003 PC game. Version 0.0.1.<br>
  Bring your own copy of the game.
</p>

<p align="center">
  <img alt="Platform: PS Vita" src="https://img.shields.io/badge/platform-PS%20Vita-2f5da8?style=flat-square">
  <a href="LICENSE"><img alt="License: GPLv3" src="https://img.shields.io/badge/license-GPLv3-5b7083?style=flat-square"></a>
  <img alt="Status: not run on a console" src="https://img.shields.io/badge/console-untested-b5483a?style=flat-square">
</p>

sh3-port runs Silent Hill 3 from the PC version's own data. A recompiler translates the game's `sh3.exe` (x86) to C, and a Win32 / Direct3D 8 / DirectInput / DirectSound compatibility layer runs it over [vitaGL](https://github.com/Rinnegatamante/vitaGL) on the Vita. It is built on [sh4vita](https://github.com/vallfrr/sh4vita) by vallfrr, the Silent Hill 4 port that came first, and uses [sh3-nx](https://github.com/cutarev/sh3-nx) by cutarev as its rendering reference.

**sh3-port contains no game data.** You need your own copy of the 2003 PC game.

> [!WARNING]
> Nothing here has been confirmed on a real PS Vita. Everything was tested on a PC (Linux, Docker, software GL) and in the Vita3K+ emulator, and emulator results are not console results. 0.0.1 is a development snapshot, not a playable release.

## What it does

Tested on a PC only (Docker image `sh3host:local`, Mesa software GL, dummy audio):

- Boots through the Konami logo, content warning, the port's credits page and the title menu
- New Game: the in-engine intro, then the first playable room (`tpf1`, Lakeside Amusement Park entrance); Heather walks (arrows / WASD) and stops at walls
- Renders that room like sh3-nx in the views compared so far, including the local spotlights (evidence, not ground truth)
- A "Textures" option (Performance / Balanced / High) and a working Resolution list
- The opening movie `op.000` plays in-game on the PC build, through the game's own DirectShow-style graph. Only the first ~90 s of 192 s were checked, and the end, skipping, the other six movies and audible audio are untested. The source change for this was still uncommitted when this was written.

## Install

0.0.1 ships no pre-built VPK, and none of these steps has been verified on a console. You need:

- a PS Vita with custom firmware
- `ur0:data/libshacccg.suprx`, the system's shader compiler, which the port compiles its shaders with. This project neither supplies nor links to it
- the 2003 PC release of Silent Hill 3: `sh3.exe` (SHA-256 `8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918`) and its `data` folder. Other builds are not supported
- on your computer: Docker, and Python 3 for the recompiler

1. Generate the C code from your own executable: `python3 recomp/x86rec.py --target sh3` (reads `game/sh3.exe` and `re/out/functions-sh3.json`, writes `port/gen-sh3/`, takes a minute or two; the output is never committed).
2. Build the package: `docker/build-vita.sh`. It writes `sh3.vpk`; title ID `SILH00003`. Install it with VitaShell.
3. Copy `sh3.exe` and the `data` folder from your game to `ux0:data/sh3/` on the Vita. If either is missing, the port shows a "GAME FILES NOT FOUND" screen.

Logs go to `ux0:data/sh3/sh4log.txt` (the name is inherited from SH4; earlier runs are kept as numbered copies).

With FTP on the console (VitaShell: SELECT once its SELECT button is set to FTP, or the vitacompanion plugin), `tools/vita.sh` does the round trip from the computer: `tools/vita.sh ip <address>` once, then `tools/vita.sh vpk` (upload `sh3.vpk` to `ux0:data/`), `tools/vita.sh install` (replace only the eboot after a rebuild), `tools/vita.sh pull` (logs, `env.txt`, screenshots and a list of crash dumps into `build/console/`), `tools/vita.sh rmdisp` (clear the game's "crashed last time" marker). This project provides no download of game files or of any Sony module.

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

## Status

What works and what is open, by milestone:

- **J0** target tooling: see `docs/PORTAGE-SH3-2026-11-11.md` §9–§11
- **J1** SIMD outside the CRI codec: done, differential test green
- **J2** linked startup to the first room: works on the Linux host
- **J3** data formats: complete (`docs/DATA-FORMATS-SH3.md`); `tools/j3_check_all.py` re-checks every format against the documented counts
- **J4** Direct3D 8 on Vita: census done (`docs/J4-D3D8-CENSUS.md`), translator not started
- **J5** native decoders: ADX/AIX/AFS decoders and the `.000` demangler in `port/src/codec/` are bit-exact against the J3 tools on the host. The runtime uses only the demangler; the audio decoders are not wired in
- **Vita build**: boots in Vita3K+ but showed a black screen at the last check. A fix for a Vita-side file read offset is in the source and has not been re-run on the emulator

Not confirmed: anything on a physical Vita, real audio output (only a dummy driver was used), performance, memory and texture VRAM cost on a Vita, saving and loading, anything past the first room.

## Building and working on it

The Vita toolchain runs in Docker (`docker/build-vita.sh`); the PC test image is `docker/host-build.sh` (`port/Makefile.linux`). Several names are inherited from the SH4 port and not renamed: the CMake target `sh4`, the `SH4_*` variables and options, `sh4log.txt`. `-DSH3=OFF` builds the inherited SH4 layout, which is not maintained here.

| Path | What's in it |
| --- | --- |
| `recomp/x86rec.py` | x86 to C recompiler (`--target sh3`) |
| `re/` | Ghidra export script and PE import lister |
| `port/` | Win32 / DX shim (`src/win32`, `src/dx`, `src/d3d8`), runtime (`src/rt`), Vita, PC and port-specific layers (`src/vita`, `src/pc`, `src/host`), vitaGL patches |
| `docker/` | VitaSDK image, `build-vita.sh`, `smoke.sh`, `host-build.sh` |
| `docs/` | Study and tracking documents |

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

## Thanks

The in-game credits page lists the first four:

- [sh4vita](https://github.com/vallfrr/sh4vita) by vallfrr, which this port is based on
- [sh3-nx](https://github.com/cutarev/sh3-nx) by cutarev, the rendering reference
- [sh2-vita](https://github.com/AngeloGalav/sh2-vita) by AngeloGalav
- The Silent Hill 3 PC Fix by Steam006 (2.8.9.1), <https://community.pcgamingwiki.com/files/file/1331>
- [vitaGL](https://github.com/Rinnegatamante/vitaGL) and vitaShaRK by Rinnegatamante, the [VitaSDK](https://vitasdk.org/) team and [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski

AI-assisted port by [AngeloGalav](https://github.com/AngeloGalav). Third-party code compiled into the port is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), with texts in [LICENSES/](LICENSES/).

## License

The repository's [LICENSE](LICENSE) is the GNU General Public License v3.0; the third-party notices record which licensing questions are still open. Bundled third-party code keeps its own license.

This is a fan project, not affiliated with or endorsed by Konami or Sony. *Silent Hill* is a trademark of Konami Digital Entertainment and PlayStation Vita is a trademark of Sony Interactive Entertainment. Silent Hill 3 and its assets are not part of this project.

## AI disclosure

Most of this port's code, documentation and commit messages was written with AI coding agents (Qwen Flash Next, a local model, and Claude Sonnet; Claude Opus is planned for validation), under direction from a person. Expect mistakes, and treat every claim as "checked on PC or emulator only" unless it says otherwise. The approach and much of the runtime come from sh4vita.
