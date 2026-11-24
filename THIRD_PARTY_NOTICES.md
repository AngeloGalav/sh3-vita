# Third-party notices — sh3-port (Silent Hill 3 for PS Vita) 0.0.1

The port uses third-party software. Its authors retain their copyrights and the terms below. These notices do not claim ownership of Silent Hill 3, its executable, its data, or the C code generated from it (`port/gen-sh3/`, never distributed). Players supply their own copy of the 2003 PC release.

Scope: this list was made from `port/CMakeLists.txt` (the Vita build, SH3 on by default) and the sources it compiles (`src/rt`, `src/win32`, `src/d3d8`, `src/dx`, `src/vita`, `src/host`, `src/third_party/tlsf`, plus `src/codec/movie000.c`, which `src/dx/movie.c` includes). It is a work in progress and does **not** certify any binary as ready for distribution. Where a license text is not in `LICENSES/`, this document says so instead of supplying one.

## Linked into the Vita executable

- **vitaGL**, Rinnegatamante and contributors: [source](https://github.com/Rinnegatamante/vitaGL). Source files identify LGPL version 3 or later. Texts: `LICENSES/LGPL-3.0.txt`, `LICENSES/GPL-3.0.txt` (from checkout `2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8`). Linked as `vitaGL`; the port's modifications are the patches in `port/patches/` (`vitagl-shared-depth.patch` is the one used by `docker/vita-sdk.Dockerfile`; `vitagl-sh4-cache.patch` and `vitagl-sh4-dxt-blocks.patch` are inherited from SH4). The exact library/patch combination must be matched to each release.
- **vitaShaRK**, Rinnegatamante and contributors: [source](https://github.com/Rinnegatamante/vitaShaRK), LGPL version 3 or later per its headers. Text: `LICENSES/vitaShaRK-LGPL-3.0.txt` (reference text; the installed archive revision is not established). It is used by `src/vita/main.c` (`shark_*`). It is separate from the user's proprietary `libshacccg.suprx`, which this project does not supply.
- **SceShaccCgExt**, Rinnegatamante: [source](https://github.com/Rinnegatamante/SceShaccCgExt), repository license **GPL version 3**. Text: `LICENSES/SceShaccCgExt-GPL-3.0.txt` (reference text; installed revision not established). It is linked into the executable. What this means for distributing the combined executable is unresolved; do not treat it as LGPL because vitaShaRK is.
- **SDL2**, Sam Lantinga and contributors: [source](https://github.com/libsdl-org/SDL), zlib license. Text: `LICENSES/SDL2-Zlib.txt` (VitaSDK package, `SDL-release-2.32.8`). The PC test build uses the system SDL2 of the Docker image; that is a development dependency and is not inventoried here.
- **math-neon**, Lachlan Tychsen-Smith, Vita port by Rinnegatamante: [source](https://github.com/Rinnegatamante/math-neon), MIT-style notice in `LICENSES/math-neon-MIT.txt`. Linked as `mathneon`; installed revision not verified.
- **VitaSDK / vita-headers** contributors: [source](https://github.com/vitasdk/vita-headers). Text: `LICENSES/vita-headers-LICENSE.txt`. The `Sce*_stub` import libraries in the link line come from this toolchain.

## Compiled from bundled source

- **pl_mpeg**, Dominic Szablewski: [source](https://github.com/phoboslab/pl_mpeg), MIT. Text: `LICENSES/MIT.txt`; note: `port/src/third_party/pl_mpeg/NOTICE.md`. Compiled into `src/dx/movie.c` for the movie path. The bundled copy carries sh4vita modifications (NEON motion compensation/IDCT, bit reading, B-frame skipping, Layer I audio) and must not be presented as unchanged upstream.
- **TLSF**, Copyright (c) 2006–2016 Matthew Conte: [source](https://github.com/mattconte/tlsf), BSD 3-clause. Text: `LICENSES/TLSF-BSD-3-Clause.txt` (copied from `port/src/third_party/tlsf/tlsf.h`). Used by `src/rt/heap.c`.

## Ported from third-party source

- **SilentHill3.WidescreenFix** (WidescreenFixesPack), ThirteenAG and contributors: [source](https://github.com/ThirteenAG/WidescreenFixesPack), GPL version 3 (the repository's `license` file at revision `7bce79ffaeee61f95db59a68b35a150cd8644223`). Text: `LICENSES/GPL-3.0.txt`. Its patch sites and logic were rewritten for the static recompiler as `tools/patch_exe.py` (byte patches applied to the user's own `sh3.exe` before recompiling) and `port/src/host/sh3_widescreen.c`; [sh3-nx](https://github.com/cutarev/sh3-nx) was used as a reference for the approach and addresses only (it has no license file, so no code was taken from it).
- **sh3vita**, vallfrr: [source](https://github.com/vallfrr/sh3vita), GPL version 3. The idea of forcing the game's `lensflare_ztest` setting (`port/src/host/sh3_lensflare.c`) comes from its `port/diag_lens_query.c`; the code here is its own.

## Toolchain runtime pulled in by the link line (`stdc++ m z`, newlib, libgcc)

No license text for these is in `LICENSES/` yet, and none is invented here:

- **zlib** (`-lz`): zlib license. Missing from `LICENSES/`.
- **newlib** (C library) and **libgcc / libstdc++** (GCC runtime library exception): BSD-style / GPL-with-exception terms from the VitaSDK toolchain. Missing from `LICENSES/`.
- **taiHEN** (`taihen_stub`): linked in `CMakeLists.txt`; the current sources contain no `tai_*` call. Whether anything of it ends up in the executable, and under what license, has not been checked. Missing from `LICENSES/`.

These must be inventoried from the exact VitaSDK installation that builds a release.

## Not compiled into the Vita build

- `port/src/codec/adx.c`, `aix.c`, `afs.c` (ADX/AIX/AFS decoders): written for this project, bit-exact with the J3 tools, not part of the Vita link at 0.0.1. `codec_selftest.c`, `sha256.c` (public-domain-style) and `png_write.c` are host test support only. `codec/pl_mpeg_impl.c` belongs to the host self-test.
- `docker/` tooling uses the `vitasdk/vitasdk` image and, for the PC test image, Debian packages with their own licenses. They are build tools and are not shipped.

## Items without a recorded origin

- LiveArea artwork in `port/livearea/` (`icon0.png`, `pic0.png`, `bg.png`, `startup.png`, `template.xml`). The artwork was supplied; its author and license are not recorded in the repository. Settle this before any release.
- The in-game credits page names vallfrr's sh4vita, cutarev's sh3-nx, AngeloGalav's sh2-vita and the Silent Hill 3 PC Fix by Steam006. No source file in `port/src` states that code was copied from sh3-nx, sh2-vita or the PC Fix, and no license text for them is in this repository. The recompiler and shim derive from sh4vita; no separate license file for it is kept here.

## Project license

A GPL-3.0 text is at the repository root (`LICENSE`). No decision on the license of the port's own source is recorded beyond that file; `LICENSES/` only preserves third-party terms.

These documents must accompany any binary distribution, together with the corresponding sources or relinking materials where the licenses above require them. The earlier SH4 audit (`docs/DISTRIBUTION-LICENSE-AUDIT.md` in the `sh3-vita` repository, not in this one) lists unresolved source, linking, generated-code and packaging questions; they carry over until answered.
