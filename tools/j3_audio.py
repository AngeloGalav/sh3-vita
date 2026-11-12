#!/usr/bin/env python3
"""SH3 audio tooling (J3): AFS archives, ADX decoder, AIX demuxer.

Formats (verified on the GOG data, 2026-11-11):
  AFS:  'AFS\\0' | u32 LE count | count x (u32 off, u32 size) | ... (entries 0x800 aligned)
  entry kinds in sound/sd.afs (378): RIFF/WAVE PCM (362), AIXF (12), raw ADX (4)
  entry kinds in sound/demo_afs/*.afs: ADX (0x80 0x00 ...) and a '78 56 34 12' container (open)
  ADX:  0x80 0x00 | u16 BE copyright_off (data at +4) | enc=3 | blk=18 | bits=4 | ch |
        u32 BE rate | u32 BE total_samples | u16 BE highpass | ... frames of 18 B/channel
  AIX:  'AIXF' | u32 BE hdr_size | ... ; chunks at 8+hdr_size: 'AIXP' | u32 BE size |
        u8 stream | u8 nstreams-1 | u16 BE payload_size | u32 BE frame_counter | payload
        (counter 0xffffffff = per-stream ADX header chunk). Each stream is a stereo/mono ADX.

Usage:
  j3_audio.py ls   FILE.afs
  j3_audio.py x    FILE.afs OUTDIR            extract every entry (wav/adx/aix/bin)
  j3_audio.py wav  FILE.afs INDEX OUT.wav     decode entry (WAV/ADX) to 16-bit PCM wav
  j3_audio.py aix  FILE.afs INDEX OUTDIR      decode every AIX stream to OUTDIR/sN.wav
"""
import math
import os
import struct
import sys
import wave


def afs_entries(path):
    f = open(path, "rb")
    if f.read(4) != b"AFS\x00":
        raise ValueError("not AFS")
    n = struct.unpack("<I", f.read(4))[0]
    tab = f.read(8 * n)
    return f, [struct.unpack_from("<II", tab, 8 * i) for i in range(n)]


def afs_read(f, ent):
    f.seek(ent[0])
    return f.read(ent[1])


def kind(b):
    if b[:4] == b"RIFF":
        return "wav"
    if b[:4] == b"AIXF":
        return "aix"
    if b[:2] == b"\x80\x00":
        return "adx"
    if b[:4] == b"\x78\x56\x34\x12":
        return "x78"
    return "bin"


# ---- ADX -------------------------------------------------------------------

def adx_header(b):
    if b[:2] != b"\x80\x00":
        raise ValueError("not ADX")
    off = struct.unpack_from(">H", b, 2)[0]
    enc, blk, bits, ch = b[4], b[5], b[6], b[7]
    rate, total = struct.unpack_from(">II", b, 8)
    cutoff = struct.unpack_from(">H", b, 0x10)[0]
    ver = b[0x12]
    loop = None
    # loop info: version 3 at +0x18 (flag, start smp, start off, end smp, end off, all u32 BE);
    # version 4 at +0x24 (after 3 u32 history words at +0x18..0x23)
    base = {3: 0x18, 4: 0x24}.get(ver)
    if base is not None and off + 4 >= base + 20 and len(b) >= base + 20:
        flag, ss, so, es, eo = struct.unpack_from(">5I", b, base)
        if flag:
            loop = dict(start_sample=ss, start_offset=so, end_sample=es, end_offset=eo)
    return dict(data_off=off + 4, enc=enc, blk=blk, bits=bits, ch=ch, rate=rate,
                total=total, cutoff=cutoff, version=ver, flags=b[0x13], loop=loop)


def adx_coefs(cutoff, rate):
    a = math.sqrt(2) - math.cos(2 * math.pi * cutoff / rate)
    bb = math.sqrt(2) - 1
    c = (a - math.sqrt((a + bb) * (a - bb))) / bb
    return int(math.floor(c * 8192)), int(math.floor(c * c * -4096))


def adx_decode_frames(frames, ch, rate, cutoff, nsamples=None):
    """frames: bytes of interleaved 18-byte frames (ch frames per 32-sample block).
    Returns list of per-channel int16 lists (type 3, 4-bit, fixed coefficients)."""
    c1, c2 = adx_coefs(cutoff, rate)
    nblocks = len(frames) // (18 * ch)
    out = [[0] * (nblocks * 32) for _ in range(ch)]
    hist = [[0, 0] for _ in range(ch)]
    for blk in range(nblocks):
        for c in range(ch):
            o = (blk * ch + c) * 18
            scale = (((frames[o] << 8) | frames[o + 1]) & 0x1FFF) + 1
            h1, h2 = hist[c]
            dst = out[c]
            base = blk * 32
            for i in range(32):
                byte = frames[o + 2 + (i >> 1)]
                n = (byte >> 4) if (i & 1) == 0 else (byte & 15)
                if n >= 8:
                    n -= 16
                s = ((n * scale) << 0) + ((c1 * h1 + c2 * h2) >> 12)
                if s > 32767:
                    s = 32767
                elif s < -32768:
                    s = -32768
                dst[base + i] = s
                h2, h1 = h1, s
            hist[c] = [h1, h2]
    if nsamples is not None:
        out = [x[:nsamples] for x in out]
    return out


def adx_decode(b):
    h = adx_header(b)
    if h["enc"] != 3 or h["bits"] != 4 or h["blk"] != 18:
        raise ValueError("unsupported ADX variant %r" % h)
    return h, adx_decode_frames(b[h["data_off"]:], h["ch"], h["rate"], h["cutoff"], h["total"])


def write_wav(path, chans, rate):
    ch = len(chans)
    n = min(len(c) for c in chans)
    w = wave.open(path, "wb")
    w.setnchannels(ch)
    w.setsampwidth(2)
    w.setframerate(rate)
    buf = bytearray()
    for i in range(n):
        for c in range(ch):
            buf += struct.pack("<h", chans[c][i])
    w.writeframes(bytes(buf))
    w.close()


# ---- AIX -------------------------------------------------------------------

def aix_streams(b):
    """Return {stream: dict(header=bytes, data=bytearray)} from an AIXF blob."""
    hs = struct.unpack_from(">I", b, 4)[0]
    p = 8 + hs
    streams = {}
    while p + 16 <= len(b) and b[p:p + 4] == b"AIXP":
        size = struct.unpack_from(">I", b, p + 4)[0]
        sid = b[p + 8]
        w = struct.unpack_from(">H", b, p + 10)[0]
        ctr = struct.unpack_from(">I", b, p + 12)[0]
        payload = b[p + 16:p + 16 + w]
        s = streams.setdefault(sid, dict(header=None, data=bytearray()))
        if ctr == 0xFFFFFFFF:
            s["header"] = bytes(payload)
        else:
            s["data"] += payload
        p += 8 + size
    return streams


def aix_decode(b):
    out = {}
    for sid, s in sorted(aix_streams(b).items()):
        hdr = s["header"]
        h = adx_header(hdr)
        out[sid] = (h, adx_decode_frames(bytes(s["data"]), h["ch"], h["rate"], h["cutoff"], h["total"]))
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, path = argv[1], argv[2]
    f, ents = afs_entries(path)
    if cmd == "ls":
        for i, e in enumerate(ents):
            b = afs_read(f, (e[0], min(e[1], 0x40)))
            print(i, hex(e[0]), e[1], kind(b))
    elif cmd == "x":
        os.makedirs(argv[3], exist_ok=True)
        for i, e in enumerate(ents):
            b = afs_read(f, e)
            open(os.path.join(argv[3], "%04d.%s" % (i, kind(b))), "wb").write(b)
    elif cmd == "wav":
        b = afs_read(f, ents[int(argv[3])])
        k = kind(b)
        if k == "wav":
            open(argv[4], "wb").write(b)
        elif k == "adx":
            h, ch = adx_decode(b)
            write_wav(argv[4], ch, h["rate"])
            print(h)
        else:
            raise SystemExit("unsupported kind " + k)
    elif cmd == "aix":
        b = afs_read(f, ents[int(argv[3])])
        os.makedirs(argv[4], exist_ok=True)
        for sid, (h, ch) in aix_decode(b).items():
            write_wav(os.path.join(argv[4], "s%d.wav" % sid), ch, h["rate"])
            print(sid, h)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
