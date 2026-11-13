# `port/src/codec` — J5 native decoders (SH3 → Vita)

Standalone, portable C99 decoders for the SH3 data the Vita port must play
natively (milestone J5). The shipped library files use only `stdint.h`,
`stddef.h`, `string.h` and `math.h`: no stdio, no malloc, no platform
headers, no x86 intrinsics — they cross-compile for the Vita unchanged
(`make vita`, proven with `arm-vita-eabi-gcc -std=c99 -pedantic -Wall
-Wextra -Werror`). Only the self-test harness may use host facilities.

## Contents

| File | What |
|---|---|
| `adx.h/.c` | CRI ADX (type 3, 4-bit ADPCM) decoder incl. loop points (v3 `+0x18`, v4 `+0x24`) |
| `aix.h/.c` | CRI AIXF demuxer: splits an AIXF entry into its ADX substreams |
| `afs.h/.c` | CRI AFS archive reader (zero-copy, zero-allocation) |
| `movie000.h/.c` | SH3 `.000` movie demangler (MSVC-CRT LCG keystream, period `0x1000`) |
| `codec_selftest.c` | host harness: `probe` / `hash` / `decrypt` / `movie` / `moviepng` |
| `sha256.*`, `png_write.*` | harness support only (hashing, zlib-free PNG writer) |
| `pl_mpeg_impl.c` | instantiates the vendored pl_mpeg (see below) |

Reference oracles (J3 tools, kept in `tools/`): `j3_audio.py` (AFS/ADX/AIX)
and `j3_movie.py` (`.000`). The C decoders are bit-exact with them — see the
acceptance record below.

## MPEG-2 evaluation

For MPEG-2 video the vendored **pl_mpeg** (`port/src/third_party/pl_mpeg`,
MIT, `LICENSES/MIT.txt`, listed in `THIRD_PARTY_NOTICES.md`) is reused: it is
the same single-file decoder the SH4 port used, already carries the sh4vita
modifications (Layer I audio, NEON paths guarded by `__ARM_NEON`, fast bit
reads), and needs no new vendoring. The harness drives it through
`plm_decode_video` (the B-frame-skip logic lives only in the high-level
`plm_decode` tick loop, so frame counting is complete). No other decoder was
needed; nothing new was vendored.

## Build

Host (x86_64, in the `sh3host:local` container):

    docker/host-build.sh make -C port/src/codec

Vita cross-compile of the library (proof of portability):

    docker run --rm -v <repo>:/work -u $(id -u):$(id -g) -w /work \
      sh3vita-sdk:local make -C port/src/codec vita VITASDK=/usr/local/vitasdk

## Self-test usage

    build/codec_selftest probe  <file.afs>          # header/loop survey per entry
    build/codec_selftest hash  <file.afs> <out|->   # sha256 of decoded PCM per stream
    build/codec_selftest decrypt <in.000> <out.mpg> # demangle + sha256 + pack-sync survey
    build/codec_selftest movie  <in.mpg>            # decode all frames: count/fps/size
    build/codec_selftest moviepng <in.mpg> <t> <out.png>

## Acceptance record (2026-11-13, host only — no Vita/console claim)

Scratch and full logs: `sh3-vita/build/codec-scratch/` (`run.log`,
`c_hash.txt` vs `ref_hash.txt`, `c_probe.txt` vs `ref_probe.txt`,
`c_movie.txt` vs `ref_movie.txt`, `c_frames.txt`, `png/`). Both sides run in
the same container so the double-precision coefficient math uses the same
libm.

- ADX/AIX: **427/427 streams bit-exact** (SHA-256 of interleaved int16 PCM)
  against `j3_audio.py`: 368 ADX in `demo_afs/*.afs` (37 archives) + 4 raw
  ADX in `sd.afs` + 55 AIX substreams of the 12 AIXF entries in `sd.afs`.
  Header/loop probe survey also identical (`PROBE_MATCH`, 1185 lines;
  138/372 ADX carry loop points, matching the J3 doc).
- Movies: all 7 `.000` files decrypt **byte-identically** to `j3_movie.py`
  (SHA-256 of the decrypted region), `bad_sync=0` on every 2048-byte sector
  and the tail is the pack-end code `00 00 01 B9` (`tail_packend=1`).
  pl_mpeg then decodes every frame:

  | movie | frames | fps | duration | size |
  |---|---|---|---|---|
  | ed_b | 8997 | 29.97 | 300.17 s | 512×384 |
  | ed_n | 9020 | 29.97 | 300.93 s | 512×384 |
  | ed_u | 10259 | 29.97 | 342.08 s | 512×384 |
  | henshin | 741 | 29.97 | 24.69 s | 512×384 |
  | huxx | 540 | 29.97 | 17.95 s | 512×384 |
  | muxx | 395 | 30.00 | 13.13 s | 512×384 |
  | op | 5760 | 29.97 | 192.09 s | 512×384 |

  PNG spot-checks (2 per movie, early + mid) reviewed: real frames (opening
  FMV, ending credits roll, transformation sequence).

Not done here: wiring into the game runtime (later milestone), movie audio
tracks (video-only decode), Vita hardware validation.
