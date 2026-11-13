#!/usr/bin/env python3
"""SH3 .anm frame-record walker and whole-data check (J3).

Grammar and numbers come from docs/DATA-FORMATS-SH3.md, section
"`.anm` character animation" (exe consumers 0x4a6d60 / 0x4a6fc0, stride function
0x473c10). Nothing here is tuned to make a count match; files that do not walk
are reported as UNEXPLAINED with the reason.

File = u32 id, then frames of `stride` bytes (stride is a per-character constant
keyed by the id, table below). A frame record is read sequentially:
  * a u32 control word at the start of the record and again before every 8th
    bone (bones 0, 8, 16, ...); 4 bits per bone, LSB first, type = nibble & 7
  * type 0/3/4/7: no data
  * type 1/5: 6 B  (3 x s16 rotation)
  * type 2/6: 6 B rotation + translation: 12 B (3 x float32) for ROOT bones
    (parent byte 0xff), 6 B (3 x half-float) for child bones
Bone count = nA of the paired .mdl, parent bytes = first nA bytes at offB
(see j3_mdl.py). A frame "fits" when the walk consumes <= stride bytes (the rest
is padding); "exact" means it consumed exactly stride bytes.

Usage:
  j3_anm.py walk FILE.anm --mdl FILE.mdl [--bones N]
  j3_anm.py check DATA_DIR [--scratch DIR] [--quiet]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import j3_arc  # noqa: E402
import j3_mdl  # noqa: E402

DEFAULT_SCRATCH = "/home/agala-ai-linux/projects/sh3-vita/build/check-scratch"

# Per-character stride, exe 0x473c10 (jump tables 0x473d74/0x473d88/0x473ddc,
# byte table 0x473e04). Source: docs/DATA-FORMATS-SH3.md, `.anm` stride table.
# Ids not listed there have stride 0 ("unused"); they are absent here on purpose.
STRIDE = {
    0x100: 0x108,   # player (form 0)
    0x101: 0,
    0x102: 0x2de, 0x103: 0x1e0, 0x104: 0x1f6,
    0x200: 0xbe, 0x201: 0x68, 0x202: 0x90, 0x203: 0xdc, 0x204: 0x96,
    0x205: 0x86, 0x206: 0xd0, 0x207: 0x74, 0x208: 0x22, 0x209: 0x2e,
    0x20a: 0x8a, 0x20b: 0x7a,
    0x210: 0xf0, 0x211: 0xd0, 0x212: 0x11c, 0x213: 0xd0, 0x214: 0x22e,
    0x215: 0x116,
    0x101c: 0x22, 0x101e: 0x8a, 0x101f: 0x11c, 0x1020: 0x46, 0x1024: 0x92,
    0x1033: 0x16, 0x1038: 0x2e, 0x103d: 0x80, 0x103e: 0x46, 0x1043: 0x34,
    0x104e: 0x16, 0x104f: 0xa8,
}
for _i in (0x20c, 0x20d, 0x20e, 0x20f):
    STRIDE[_i] = 0

PLAYER_ID = 0x100
PLAYER_MDL = "data/pcchr/pl/chhaa.mdl"   # 14 .mdl share id 0x100; the docs pair the player with chhaa
PLAYER_BONES = 48   # docs: the 0x108 frame record animates the first 48 of chhaa's 73 bones


def mdl_info(mdl_bytes):
    """-> (id, nA, parent bytes list of length nA). Uses j3_mdl's own parsers."""
    o = j3_mdl.parse_outer(mdl_bytes)
    d = j3_mdl.parse_model(mdl_bytes, o["dataoff"], o["size"] - o["dataoff"])
    nA = d["A_n"]
    base = o["dataoff"] + d["B"]
    par = list(mdl_bytes[base:base + nA])
    if len(par) != nA:
        raise ValueError("parent bytes truncated")
    return o["id"], nA, par


def walk_frame(b, off, parents):
    """Walk one record starting at absolute offset `off`; return bytes consumed
    or raise ValueError (overrun of the file)."""
    pos = off
    n = len(b)
    ctrl = 0
    for i, par in enumerate(parents):
        j = i & 7
        if j == 0:
            if pos + 4 > n:
                raise ValueError("control word past EOF")
            ctrl = struct.unpack_from("<I", b, pos)[0]
            pos += 4
        t = (ctrl >> (4 * j)) & 7
        if t in (1, 5):
            pos += 6
        elif t in (2, 6):
            pos += 6 + (12 if par == 0xFF else 6)
    if pos > n:
        raise ValueError("record runs past EOF")
    return pos - off


def walk(b, parents, stride):
    """Walk every frame. Returns a dict (see keys below)."""
    if stride <= 0:
        raise ValueError("stride 0")
    body = len(b) - 4
    nfull, rem = divmod(body, stride)
    fit = exact = 0
    maxpad = 0
    first_bad = None
    for f in range(nfull):
        off = 4 + f * stride
        try:
            used = walk_frame(b, off, parents)
        except ValueError as exc:
            if first_bad is None:
                first_bad = (f, str(exc))
            continue
        if used <= stride:
            fit += 1
            if used == stride:
                exact += 1
            maxpad = max(maxpad, stride - used)
        elif first_bad is None:
            first_bad = (f, "consumed %d > stride %d" % (used, stride))
    tail = None
    if rem:
        off = 4 + nfull * stride
        try:
            used = walk_frame(b, off, parents)
            tail = dict(used=used, rem=rem, ends_at_eof=(used == rem), fits=(used <= rem))
        except ValueError as exc:
            tail = dict(used=None, rem=rem, ends_at_eof=False, fits=False, err=str(exc))
    return dict(stride=stride, nframes=nfull, rem=rem, fit=fit, exact=exact,
                maxpad=maxpad, first_bad=first_bad, tail=tail,
                all_fit=(fit == nfull), all_exact=(exact == nfull))


def describe(w, nbones):
    s = "nframes=%d stride=%#x bones=%d fit=%d/%d exact=%d/%d maxpad=%d" % (
        w["nframes"], w["stride"], nbones, w["fit"], w["nframes"],
        w["exact"], w["nframes"], w["maxpad"])
    s += " | every frame exactly stride: %s" % ("YES" if w["all_exact"] else "NO")
    s += " | all frames within stride (padding allowed): %s" % ("YES" if w["all_fit"] else "NO")
    if w["rem"]:
        t = w["tail"]
        s += " | trailing %d B after full frames: next record uses %s B (%s)" % (
            w["rem"], t["used"], "ends exactly at EOF" if t["ends_at_eof"] else
            ("fits" if t["fits"] else "does NOT fit"))
    if w["first_bad"]:
        s += " | first bad frame %d: %s" % w["first_bad"]
    return s


# ---------------------------------------------------------------- check

def collect(data_dir):
    archives, _ = j3_arc.load_all(data_dir)
    anms, mdls = [], []
    for a in archives.values():
        for i, e in enumerate(a.entries):
            p = (e.path or "").lower()
            if p.endswith(".anm"):
                anms.append((a, i, e.path))
            elif p.endswith(".mdl"):
                mdls.append((a, i, e.path))
    return anms, mdls


def classify(name, b, mdl_by_id):
    """-> (bucket, detail dict). Bucket order is fixed and documented in main()."""
    fid = struct.unpack_from("<I", b, 0)[0]
    stride = STRIDE.get(fid)
    r = dict(name=name, id=fid, size=len(b), stride=stride)
    if stride is None:
        return "UNEXPLAINED", dict(r, why="id %#x not in stride table" % fid)
    if stride == 0:
        return "UNEXPLAINED", dict(r, why="stride 0 for id %#x" % fid)
    r["size_formula"] = ((len(b) - 4) % stride == 0)
    cands = mdl_by_id.get(fid, [])
    if not cands:
        return "NO_MDL", dict(r, why="no .mdl with id %#x" % fid)
    # several .mdl with the same id: prefer the one whose basename is a prefix/equal
    base = os.path.splitext(os.path.basename(name))[0]
    pick = [c for c in cands if os.path.splitext(os.path.basename(c[0]))[0] == base] or cands
    if fid == PLAYER_ID:
        pick = [c for c in cands if c[0] == PLAYER_MDL] or pick
    if len(pick) > 1:
        return "UNEXPLAINED", dict(r, why="ambiguous pairing: %d .mdl share id %#x" % (len(cands), fid))
    mpath, nA, par = pick[0]
    r["mdl"] = mpath
    r["nA"] = nA
    w = walk(b, par, stride)
    r["walk"] = w
    r["bones"] = nA
    if w["all_fit"] and not w["rem"]:
        return "PAIRED_FIT", r
    if w["all_fit"] and w["rem"] and w["tail"]["fits"]:
        return "UNPADDED_TAIL", r
    if fid == PLAYER_ID:
        w48 = walk(b, par[:PLAYER_BONES], stride)
        r["walk48"] = w48
        r["walk_nA"] = w
        if w48["all_fit"] and not w48["rem"]:
            r["bones"] = PLAYER_BONES
            return "PLAYER_48", r
    why = "nA=%d walk: fit %d/%d" % (nA, w["fit"], w["nframes"])
    if w["first_bad"]:
        why += ", first bad frame %d (%s)" % w["first_bad"]
    if w["rem"]:
        why += ", trailing %d B %s" % (w["rem"], "fits" if w["tail"]["fits"] else "does not fit")
    return "UNEXPLAINED", dict(r, why=why)


def run_check(data_dir, scratch=DEFAULT_SCRATCH, quiet=False, out=print):
    """Extract each .anm to scratch (group__subdir__name), walk it, delete it.
    Returns (results, buckets) where results = [(bucket, detail)]."""
    anms, mdls = collect(data_dir)
    mdl_by_id = {}
    for a, i, p in mdls:
        b = a.payload(i)
        try:
            mid, nA, par = mdl_info(b)
        except Exception:
            continue
        mdl_by_id.setdefault(mid, []).append((p, nA, par))
    d = os.path.join(scratch, "anm")
    os.makedirs(d, exist_ok=True)
    results = []
    for a, i, p in sorted(anms, key=lambda t: t[2]):
        rel = p[len("data/chr/"):] if p.startswith("data/chr/") else p
        fn = rel.replace("/", "__")                  # dir + basename: no collisions
        tmp = os.path.join(d, fn)
        try:
            with open(tmp, "wb") as f:
                f.write(a.payload(i))
            with open(tmp, "rb") as f:
                b = f.read()
        finally:
            try:
                os.remove(tmp)
            except OSError:
                pass
        bucket, det = classify(p, b, mdl_by_id)
        results.append((bucket, det))
    buckets = {}
    for bk, det in results:
        buckets.setdefault(bk, []).append(det)
    return results, buckets


BUCKET_ORDER = ["PAIRED_FIT", "PLAYER_48", "NO_MDL", "UNPADDED_TAIL", "UNEXPLAINED"]
BUCKET_DOC = {
    "PAIRED_FIT": "walks at stride with the paired .mdl's nA bones (docs: 56)",
    "PLAYER_48": "id 0x100: fails with nA=73, fits using the first 48 parent bytes (docs: 31)",
    "NO_MDL": "no .mdl with the same id (docs: 3 ded1 files)",
    "UNPADDED_TAIL": "all full frames fit and the trailing partial record fits (docs: 2 test/ files)",
    "UNEXPLAINED": "none of the above",
}


def summarize(results, buckets, out=print):
    out("%-14s %5s  %s" % ("bucket", "files", "meaning"))
    for bk in BUCKET_ORDER:
        out("%-14s %5d  %s" % (bk, len(buckets.get(bk, [])), BUCKET_DOC[bk]))
    out("%-14s %5d" % ("total", len(results)))
    sf = sum(1 for _, d in results if d.get("size_formula"))
    known = sum(1 for _, d in results if "size_formula" in d)
    out("size formula size = 4 + nframes*stride: %d of %d files with a known stride" % (sf, known))
    exact_all = sum(1 for bk, d in results if "walk" in d and
                    (d["walk"]["all_exact"] if d["bones"] == d.get("nA") else d["walk48"]["all_exact"]))
    out("files where every full frame is EXACTLY stride bytes (no padding): %d" % exact_all)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    w = sub.add_parser("walk")
    w.add_argument("anm")
    w.add_argument("--mdl", required=True)
    w.add_argument("--bones", type=int, help="override bone count (first N parent bytes)")
    c = sub.add_parser("check")
    c.add_argument("data_dir")
    c.add_argument("--scratch", default=DEFAULT_SCRATCH)
    c.add_argument("--quiet", action="store_true", help="summary only, no per-file lines")
    args = ap.parse_args()

    if args.cmd == "walk":
        b = open(args.anm, "rb").read()
        mid, nA, par = mdl_info(open(args.mdl, "rb").read())
        fid = struct.unpack_from("<I", b, 0)[0]
        stride = STRIDE.get(fid)
        print("%s: id %#x (mdl id %#x), nA=%d" % (os.path.basename(args.anm), fid, mid, nA))
        if not stride:
            print("no stride for id %#x" % fid)
            return 1
        nb = args.bones or nA
        wk = walk(b, par[:nb], stride)
        print(describe(wk, nb))
        return 0 if wk["all_fit"] else 1

    results, buckets = run_check(args.data_dir, args.scratch)
    if not args.quiet:
        for bk, det in results:
            line = "%-13s %-44s id=%#-6x size=%-7d" % (bk, det["name"][len("data/chr/"):], det["id"], det["size"])
            if "walk" in det:
                wk = det["walk48"] if bk == "PLAYER_48" else det["walk"]
                line += " " + describe(wk, det["bones"])
                if bk == "PLAYER_48":
                    line += " | with nA=%d: fit %d/%d" % (det["nA"], det["walk_nA"]["fit"], det["walk_nA"]["nframes"])
            elif det.get("why"):
                line += " " + det["why"]
            print(line)
        print()
    summarize(results, buckets)
    un = buckets.get("UNEXPLAINED", [])
    for d in un:
        print("UNEXPLAINED %s: %s" % (d["name"], d.get("why")))
    return 0 if not un else 1


if __name__ == "__main__":
    sys.exit(main())
