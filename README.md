<h1 align="center">sh3-port: Silent Hill 3 for PlayStation Vita</h1>

<p align="center">
  An experimental static-recompilation port of the 2003 PC game. Version 0.0.1.<br>
  Bring your own copy of the game.
</p>

<p align="center">
  <img alt="Platform: PS Vita" src="https://img.shields.io/badge/platform-PS%20Vita-2f5da8?style=flat-square">
  <a href="LICENSE"><img alt="License: GPLv3" src="https://img.shields.io/badge/license-GPLv3-5b7083?style=flat-square"></a>
  <img alt="Status: early console tests" src="https://img.shields.io/badge/console-early%20tests-c98a2b?style=flat-square">
</p>

sh3-port runs Silent Hill 3 from the PC version's own data. A recompiler translates the game's `sh3.exe` (x86) to C, and a Win32 / Direct3D 8 / DirectInput / DirectSound compatibility layer runs it over [vitaGL](https://github.com/Rinnegatamante/vitaGL) on the Vita. It is built on [sh4vita](https://github.com/vallfrr/sh4vita) by vallfrr, the Silent Hill 4 port that came first, and uses [sh3-nx](https://github.com/cutarev/sh3-nx) by cutarev as its rendering reference.

**sh3-port contains no game data.** You need your own copy of the 2003 PC game.

> [!WARNING]
> 0.0.1 is a development snapshot, not a finished release. It has been played on a real PS Vita through the opening and the first areas only. See [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md) before trying it.

## What it does

On a real PS Vita (November 2026, extended memory budget, `libshacccg.suprx` installed):

- Boots through the Konami logo, the content warning and the port's credits page, plays the opening movie (about 20 fps from `op.000`; 30 fps with picture and sound from a converted `op.mp4`, see Install) and reaches the title menu (New Game / Load / Options)
- New Game: the intro at Lakeside Amusement Park and the first areas, with music and cutscene audio
- Renders at the Vita's native 960x544 in widescreen; the frame rate is capped at 30 fps and holds it most of the time (heavier spots drop to about 20)
- A "Textures" option (Performance / Balanced / High) and the Options pages switchable with L / R

Built and checked on the PC but not yet heard or seen on the console: in-game sound effects (footsteps, doors, items), the Vita button layout below, the credits page before the opening movie and the title menu without Exit.

## Install

0.0.1 ships no pre-built VPK, and none of these steps has been verified on a console. You need:

- a PS Vita with custom firmware
- `ur0:data/libshacccg.suprx`, the system's shader compiler, which the port compiles its shaders with. This project neither supplies nor links to it
- the 2003 PC release of Silent Hill 3: `sh3.exe` (SHA-256 `8ff8f74806f55843b9bc277508b17c8926999f292c65dbe7949325087d460918`) and its `data` folder. Other builds are not supported
- on your computer: Docker, and Python 3 for the recompiler

1. Apply the port's byte patches (widescreen, title menu without Exit) to a copy of your executable: `python3 tools/patch_exe.py <your sh3.exe> game/sh3.exe re/out/functions-sh3.json` (checks every site against the original bytes and adds the one extra function the patches call to `functions-sh3.json`; run it on an unpatched copy).
2. Generate the C code from the patched copy: `python3 recomp/x86rec.py --target sh3` (reads `game/sh3.exe` and `re/out/functions-sh3.json`, writes `port/gen-sh3/`, takes a minute or two; the output is never committed).
3. Build the package: `docker/build-vita.sh`. It writes `sh3.vpk`; title ID `SILH00003`. Install it with VitaShell.
4. Copy `sh3.exe` and the `data` folder from your game to `ux0:data/sh3/` on the Vita. If either is missing, the port shows a "GAME FILES NOT FOUND" screen. Keep the movie files (`data/movie/*.000`) unless you convert them as below: a converted `.mp4` can then replace its `.000`.

Optional, for full-rate movies on the Vita: `python3 tools/convert_movies.py "<your game>/data/movie"` (needs ffmpeg and ffprobe) demangles each `.000` with the verified reference algorithm and encodes an MP4 (H.264 High level 3.1, yuv420p + AAC-LC 48 kHz stereo, duration verified) next to it. Copy those `.mp4` files to `ux0:data/sh3/data/movie/` and the port plays them with the Vita's hardware video decoder (sceAvPlayer) instead of decoding in software: steady 30 fps and clean audio even when the memory card reads slowly. Without the files the built-in decoder is used unchanged. The converted files are derived from game data: never commit them, never share them.

Logs go to `ux0:data/sh3/sh4log.txt` (the name is inherited from SH4; earlier runs are kept as numbered copies).

With FTP on the console (VitaShell: SELECT once its SELECT button is set to FTP, or the vitacompanion plugin), `tools/vita.sh` does the round trip from the computer: `tools/vita.sh ip <address>` once, then `tools/vita.sh vpk` (upload `sh3.vpk` to `ux0:data/`), `tools/vita.sh install` (replace only the eboot after a rebuild), `tools/vita.sh pull` (logs, `env.txt`, screenshots and a list of crash dumps into `build/console/`), `tools/vita.sh rmdisp` (clear the game's "crashed last time" marker). For a crash, `tools/vita.sh diag on` before the run uploads `tools/env-diag.txt` as `env.txt`: the log is written line by line (`SH3_LOGSYNC=1`, slower) so it keeps its last lines, with stats every 2 s and large VRAM drops; `diag off` removes it. This project provides no download of game files or of any Sony module.

## Controls

| Button | In game | In menus |
|---|---|---|
| Cross | Action / examine; with R held: attack | Confirm |
| Circle | Flashlight on / off | Back |
| Square | Run (with R held: guard) | |
| Triangle | Map | |
| L | Camera behind Heather | Previous Options page |
| R | Ready the weapon (aim) | Next Options page |
| START | Inventory | Skip a movie |
| SELECT | Pause menu | |
| Left stick / d-pad | Move | Move the cursor |
| Right stick | Camera | |

These are the game's own pad defaults, rearranged for the Vita. If the game has written a `key.ini` to `ux0:data/sh3/`, that file wins: delete it to get this layout back.

## Options

The game renders at the Vita's native 960x544 (a port of ThirteenAG's SilentHill3.WidescreenFix, GPL-3.0, following sh3-nx's approach): gameplay is Hor+, the interface stays in a centred 4:3 area, and Options → Configuration lists that single resolution. The pad's L and R change the Options pages.

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

Options → Configuration → Resolution shows the single 960x544 mode the port renders at.

## Status

What works and what is open, by milestone:

- **J0** target tooling: see `docs/PORTAGE-SH3-2026-11-11.md` §9–§11
- **J1** SIMD outside the CRI codec: done, differential test green
- **J2** linked startup to the first room: works on the Linux host
- **J3** data formats: complete (`docs/DATA-FORMATS-SH3.md`); `tools/j3_check_all.py` re-checks every format against the documented counts
- **J4** Direct3D 8 on Vita: census done (`docs/J4-D3D8-CENSUS.md`), translator not started
- **J5** native decoders: ADX/AIX/AFS decoders and the `.000` demangler in `port/src/codec/` are bit-exact against the J3 tools on the host. The runtime uses only the demangler; the audio decoders are not wired in
- **Vita build**: played on a real PS Vita (November 2026) through the opening and the first areas, widescreen 960x544, 30 fps cap. It needs the extended memory budget (set in the package) and `libshacccg.suprx`. Open problems: [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md)

Not confirmed: saving and loading a game, the other movies and endings, anything past the first areas, long sessions, texture VRAM cost of the Textures settings on a Vita.

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
| `SH3_MV_MP4=0` | Vita: do not play `<name>.mp4` next to a `.000` with sceAvPlayer; always use the software decoder. Default: the MP4 is used when present (see "Preparation"). |
| `SH3_MV_FEED=0` | Vita, MP4 playback: stop feeding the game's own movie renderer black samples (default on: without them the game's movie loop runs at 10 fps). |
| `SH3_MV_TEX=phy` / `SH3_MV_APPUTIL=0` / `SH4_CDRAM_KEEP_MB=N` | Vita, MP4 playback: decoder frame memory in phycont instead of cdram / skip `sceAppUtilInit` / cdram left free for the decoder (default 32 MB). Defaults are the console-tested setup. |
| `SH3_MV_PRIO=0xA0` | Vita, MP4 playback: base priority of the sceAvPlayer threads (a thread priority, 0-255). |
| `SH3_NO_CREDITS=1` | Skips the port's credits page before the opening movie (scripted runs). |
| `SH3_FPS=60` | Vita: turns the 30 fps cap off (the game then swings between 60 and 20–30 fps). |
| `SH4_INTERP=1` | Vita: turns on the frame interpolation inherited from SH4 (adds blended frames; off for SH3 because it undid the 30 fps cap). |
| `SH3_NATVERIFY=1` | Runs the recompiled originals of the native math functions too and logs any mismatch (`[nat]`). |
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
- [SilentHill3.WidescreenFix](https://github.com/ThirteenAG/WidescreenFixesPack) by ThirteenAG, ported for the widescreen
- [vitaGL](https://github.com/Rinnegatamante/vitaGL) and vitaShaRK by Rinnegatamante, the [VitaSDK](https://vitasdk.org/) team and [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski

AI-assisted port by [AngeloGalav](https://github.com/AngeloGalav). Third-party code compiled into the port is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), with texts in [LICENSES/](LICENSES/).

## License

The repository's [LICENSE](LICENSE) is the GNU General Public License v3.0; the third-party notices record which licensing questions are still open. Bundled third-party code keeps its own license.

This is a fan project, not affiliated with or endorsed by Konami or Sony. *Silent Hill* is a trademark of Konami Digital Entertainment and PlayStation Vita is a trademark of Sony Interactive Entertainment. Silent Hill 3 and its assets are not part of this project.

## AI disclosure

Most of this port's code, documentation and commit messages was written with AI coding agents (Qwen Flash Next, a local model, Claude Sonnet and Claude Opus), under direction from a person. Expect mistakes, and treat every claim as "checked on PC only" unless it says it was seen on a console. The approach and much of the runtime come from sh4vita.
