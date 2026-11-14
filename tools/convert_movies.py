#!/usr/bin/env python3
"""Convert SH3 `.000` movies to MP4 (H.264 + AAC) for the Vita port's sceAvPlayer path.

The Vita port plays `<name>.mp4` with the hardware decoder when it sits next to
`<name>.000` in the game's movie folder; without it the built-in pl_mpeg path is
used. Run this on a PC that owns a copy of the game, then copy the generated
`.mp4` files to `ux0:data/sh3/movie/` on the Vita (same folder as the `.000`).

The demangle step is the verified reference algorithm (j3_movie.py, bit-exact
with port/src/codec/movie000.c): the 0x8000 header carries an MSVC-LCG seed, the
data region from 0x8000 is XORed with the periodic keystream. The plaintext is a
MPEG-2 program stream (MPEG-1 video + MP2 audio), which ffmpeg demuxes directly.

Encoded video targets what the Vita's sceAvPlayer hardware decoder accepts:
H.264 High profile, level 3.1 (well under the 4.1 ceiling), yuv420p, closed GOP,
constant frame rate taken from the source. Audio: AAC-LC 48 kHz stereo (the
port's mixer runs at 48 kHz, so no resampling is needed). Duration is verified
against the source within 0.25 s.

Never commit or share the converted files: they are derived from game data.

Usage:
  convert_movies.py GAME_DIR [--out DIR] [--only NAME]... [--crf 20] [--check]
  GAME_DIR  folder containing the game's data/movie/*.000 files
  --out DIR where to write the .mp4 files (default: next to each .000)
  --only N  convert only movie N (base name, no extension); repeatable
  --crf N   x264 quality (default 20; lower = better and bigger)
  --check   demangle + verify pack sync + print stream info, convert nothing

Exact ffmpeg command used (per movie, after the demangle):
  ffmpeg -y -i IN.mpg -map 0:v:0 -map 0:a:0? \
      -c:v libx264 -profile:v high -level 3.1 -pix_fmt yuv420p -fps_mode cfr -r FPS \
      -preset slow -crf 20 -g 60 -keyint_min 30 -sc_threshold 0 -bf 2 \
      -c:a aac -ar 48000 -ac 2 -b:a 160k -movflags +faststart -shortest OUT.mp4
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import j3_movie  # verified .000 demangler (bit-exact with port/src/codec/movie000.c)


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def ffprobe(path, *args):
    r = run(["ffprobe", "-v", "error", *args, path])
    if r.returncode != 0:
        raise RuntimeError("ffprobe failed on %s: %s" % (path, r.stderr.strip()))
    return r.stdout.strip()


def probe(path):
    info = {}
    info["duration"] = float(ffprobe(path, "-show_entries", "format=duration",
                                     "-of", "default=noprint_wrappers=1:nokey=1"))
    v = ffprobe(path, "-select_streams", "v:0", "-show_entries",
                "stream=codec_name,width,height,avg_frame_rate,pix_fmt",
                "-of", "default=noprint_wrappers=1")
    for line in v.splitlines():
        k, _, val = line.partition("=")
        info["v_" + k] = val
    a = ffprobe(path, "-select_streams", "a:0", "-show_entries",
                "stream=codec_name,sample_rate,channels", "-of", "default=noprint_wrappers=1")
    if a:
        info["has_audio"] = True
        for line in a.splitlines():
            k, _, val = line.partition("=")
            info["a_" + k] = val
    else:
        info["has_audio"] = False
    return info


def convert(src000, out_mp4, crf, keep_tmp=False):
    name = os.path.splitext(os.path.basename(src000))[0]
    tmp = tempfile.NamedTemporaryFile(suffix=".mpg", delete=False)
    tmp.close()
    try:
        j3_movie.decrypt(src000, tmp.name)
        src = probe(tmp.name)
        fps = src.get("v_avg_frame_rate") or "30/1"
        if "/" in fps:
            n, _, d = fps.partition("/")
            fps = "%g" % (float(n) / float(d or 1)) if float(d or 1) else "30"
        cmd = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", tmp.name,
               "-map", "0:v:0", "-map", "0:a:0?",
               "-c:v", "libx264", "-profile:v", "high", "-level", "3.1",
               "-pix_fmt", "yuv420p", "-fps_mode", "cfr", "-r", fps,
               "-preset", "slow", "-crf", str(crf),
               "-g", "60", "-keyint_min", "30", "-sc_threshold", "0", "-bf", "2"]
        if src["has_audio"]:
            cmd += ["-c:a", "aac", "-ar", "48000", "-ac", "2", "-b:a", "160k"]
        cmd += ["-movflags", "+faststart", "-shortest", out_mp4]
        r = run(cmd)
        if r.returncode != 0:
            raise RuntimeError("%s: ffmpeg failed: %s" % (name, r.stderr.strip()))
        out = probe(out_mp4)
        d = out["duration"] - src["duration"]
        flag = "" if abs(d) <= 0.25 else "  WARNING: duration drift %.2f s" % d
        print("%-8s %sx%s %s fps, %s audio %s Hz -> %s: %.1f MB -> %.1f MB (%.2f s vs %.2f s)%s"
              % (name, src["v_width"], src["v_height"], fps,
                 src.get("a_codec_name", "none"), src.get("a_sample_rate", "-"),
                 os.path.basename(out_mp4),
                 os.path.getsize(src000) / 1e6, os.path.getsize(out_mp4) / 1e6,
                 out["duration"], src["duration"], flag))
        return abs(d) <= 0.25
    finally:
        if not keep_tmp:
            os.unlink(tmp.name)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("game_dir", help="folder with the game's data/movie/*.000")
    ap.add_argument("--out", default=None, help="output folder (default: next to each .000)")
    ap.add_argument("--only", action="append", default=None, help="movie base name (repeatable)")
    ap.add_argument("--crf", default="20", help="x264 CRF (default 20)")
    ap.add_argument("--check", action="store_true", help="demangle + verify, no conversion")
    a = ap.parse_args()

    files = sorted(f for f in os.listdir(a.game_dir) if f.lower().endswith(".000"))
    if a.only:
        files = [f for f in files if os.path.splitext(f)[0] in a.only]
    if not files:
        print("no .000 files found in %s" % a.game_dir)
        return 1

    ok = True
    for f in files:
        src = os.path.join(a.game_dir, f)
        base = os.path.splitext(f)[0]
        if a.check:
            tmp = tempfile.NamedTemporaryFile(suffix=".mpg", delete=False)
            tmp.close()
            try:
                j3_movie.decrypt(src, tmp.name)
                info = probe(tmp.name)
                print("%-8s %sx%s %s %s fps, audio %s %s Hz, %.2f s, %.1f MB"
                      % (base, info["v_width"], info["v_height"], info["v_codec_name"],
                         info.get("v_avg_frame_rate", "?"), info.get("a_codec_name", "none"),
                         info.get("a_sample_rate", "-"), info["duration"],
                         os.path.getsize(src) / 1e6))
            finally:
                os.unlink(tmp.name)
            continue
        out_dir = a.out or a.game_dir
        os.makedirs(out_dir, exist_ok=True)
        ok = convert(src, os.path.join(out_dir, base + ".mp4"), a.crf) and ok
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
