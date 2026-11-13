#!/usr/bin/env python3
"""Run every J3 tool over the whole SH3 data set and print a summary table.

For each format: files found, parsed OK, failed (first 5 names), and a
MATCH / MISMATCH verdict against the count written in docs/DATA-FORMATS-SH3.md.

It only imports the existing j3_*.py tools (nothing is modified). Archive
payloads are parsed in memory, so no bulk extraction happens. The only files
written are one JSON report and a few temporary .mdl copies (deleted right
after use), both under the scratch directory.

Usage:
  python3 tools/j3_check_all.py DATA_DIR [--quick] [--jobs N] [--scratch DIR]
  DATA_DIR = ".../Silent Hill 3/data"   (the folder holding arc.arc)

--quick checks only a sample (8 entries per format, the 6 smallest ADX, the
smallest movie; `arc validate` always runs in full). Counts of a sample cannot
be compared with the docs, so verdicts read SAMPLE.
Exit code 0 when no real failures and (full mode) no MISMATCH, else 1.
"""
import argparse
import contextlib
import glob
import io
import json
import os
import re
import sys
import time
import traceback
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import j3_arc    # noqa: E402
import j3_audio  # noqa: E402
import j3_anm    # noqa: E402
import j3_cam    # noqa: E402
import j3_cld    # noqa: E402
import j3_kg2    # noqa: E402
import j3_mdl    # noqa: E402
import j3_mes    # noqa: E402
import j3_movie  # noqa: E402

DEFAULT_SCRATCH = "/home/agala-ai-linux/projects/sh3-vita/build/check-scratch"
QUICK_PER_FORMAT = 8       # entries per format in --quick
QUICK_ADX = 6              # smallest ADX entries in --quick
QUICK_MOVIES = 1           # smallest movie in --quick


class Row:
    """One result line: label, found, ok, failures [(name, why)], expected."""

    def __init__(self, label, expected_found=None, expected_ok=None, note="",
                 ok_is_na=False):
        self.label = label
        self.found = 0
        self.ok = 0
        self.fails = []
        self.expected_found = expected_found   # None = no doc number
        self.expected_ok = expected_ok
        self.note = note
        self.ok_is_na = ok_is_na
        self.checked = None                    # set when only a sample ran
        self.extra = {}

    def good(self):
        self.ok += 1

    def bad(self, name, why):
        self.fails.append((name, why))

    def verdict(self, quick):
        if quick:
            return "SAMPLE"
        if self.expected_found is None:
            return "n/a"
        okv = self.ok if not self.ok_is_na else self.expected_ok
        found_match = self.found == self.expected_found
        ok_match = (self.expected_ok is None) or self.ok_is_na or (okv == self.expected_ok)
        return "MATCH" if (found_match and ok_match and not self.fails) else "MISMATCH"


def short(exc):
    return "%s: %s" % (type(exc).__name__, str(exc)[:80])


# ---------------------------------------------------------------- worker fns
# Module-level so ProcessPoolExecutor can pickle them.

def _adx_job(task):
    path, idx = task
    name = "%s[%d]" % (os.path.basename(path), idx)
    try:
        f, ents = j3_audio.afs_entries(path)
        b = j3_audio.afs_read(f, ents[idx])
        f.close()
        h, chans = j3_audio.adx_decode(b)
        n = min(len(c) for c in chans)
        if n != h["total"]:
            return name, "decoded %d samples, header says %d" % (n, h["total"])
        return name, None
    except Exception as exc:
        return name, short(exc)


def _movie_job(path):
    name = os.path.basename(path)
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf):
            rc = j3_movie.check(path)
    except Exception as exc:
        return name, 0, -1, short(exc)
    m = re.search(r"(\d+) sectors, (\d+) bad", buf.getvalue())
    if not m:
        return name, 0, -1, "unparsable output: " + buf.getvalue()[:80]
    tot, bad = int(m.group(1)), int(m.group(2))
    warn = "WARNING" in buf.getvalue()
    return name, tot, bad, ("padding block did not decrypt to zeros" if warn else None)


# ------------------------------------------------------------------ checkers

def run_arc_validate(data_dir, quick):
    row = Row("arc validate (archives)", 30, 30)
    buf = io.StringIO()
    ns = argparse.Namespace(data_dir=data_dir)
    try:
        with contextlib.redirect_stdout(buf):
            j3_arc.cmd_validate(ns)
    except Exception as exc:
        row.bad("cmd_validate", short(exc))
        return row, None
    out = buf.getvalue()
    lines = [l for l in out.splitlines() if re.match(r"^\S+\s+\d+ entries", l)]
    row.found = len(lines)
    for l in lines:
        if l.rstrip().endswith("OK"):
            row.good()
        else:
            row.bad(l.split()[0], "FAIL")
    m = re.search(r"(\d+) archives, (\d+) entries", out)
    ent_row = Row("arc validate (entries)", 2796, 2796)
    if m:
        ent_row.found = int(m.group(2))
        ent_row.ok = ent_row.found if "ALL OK" in out and not row.fails else 0
        if "ALL OK" not in out:
            ent_row.bad("summary", "no 'ALL OK' in output")
    else:
        ent_row.bad("summary", "no summary line")
    return row, ent_row


def by_ext(archives, ext):
    """[(archive, idx, path)] for all entries whose path ends with ext."""
    out = []
    for a in archives.values():
        for i, e in enumerate(a.entries):
            if e.path and e.path.lower().endswith(ext):
                out.append((a, i, e.path))
    return out


def sample(items, quick):
    if not quick:
        return items
    step = max(1, len(items) // QUICK_PER_FORMAT)
    return items[::step][:QUICK_PER_FORMAT]


def run_generic(archives, ext, row, parse_fn, quick, skip_empty=False):
    items = by_ext(archives, ext)
    if skip_empty:   # docs count non-empty files only; zero-byte ones are noted
        row.extra["zero_byte"] = sum(1 for a, i, p in items if a.entries[i].size == 0)
        items = [t for t in items if t[0].entries[t[1]].size > 0]
    row.found = len(items)
    todo = sample(items, quick)
    row.checked = len(todo)
    for a, i, p in todo:
        d = a.payload(i)
        try:
            parse_fn(d, p)
            row.good()
        except Exception as exc:
            row.bad(os.path.basename(p), short(exc))
    return row


def p_map(d, p):
    inner_off, secs = j3_arc.map_sections(d)
    if not secs:
        raise ValueError("no top-level sections")
    subs = j3_arc.walk_subs(d[inner_off:])
    last = subs[-1]
    if last["px"] + last["size"] != len(d) - inner_off:
        raise ValueError("inner container does not end at EOF")
    j3_arc.decode_container(d[inner_off:])


def p_cld(d, p):
    j3_cld.parse(d)


def p_cam(d, p):
    if len(d) % 0x80:
        raise ValueError("size %d not multiple of 128" % len(d))
    CAM_RECORDS.append(len(j3_cam.records(d)))


CAM_RECORDS = []


def p_kg2(d, p):
    j3_kg2.parse(d)


def p_mes(d, p):
    import struct
    first = struct.unpack_from("<H", d, 0)[0]
    if first == 0 or first > len(d):
        raise ValueError("bad table size %d" % first)
    offs = struct.unpack_from("<%dH" % (first // 2), d, 0)
    if any(o > len(d) for o in offs):
        raise ValueError("group offset past EOF")
    j3_mes.decode_text(d[first:])


def run_mdl(archives, scratch, quick):
    """Use the tool's own survey() on a temporary copy, one file at a time."""
    row = Row("mdl", 157, 157)
    items = by_ext(archives, ".mdl")
    row.found = len(items)
    todo = sample(items, quick)
    row.checked = len(todo)
    tmpd = os.path.join(scratch, "mdl")
    os.makedirs(tmpd, exist_ok=True)
    for a, i, p in todo:
        tmp = os.path.join(tmpd, os.path.basename(p))
        try:
            with open(tmp, "wb") as f:
                f.write(a.payload(i))
            with contextlib.redirect_stdout(io.StringIO()):
                j3_mdl.survey([tmp])
            row.good()
        except Exception as exc:
            row.bad(os.path.basename(p), short(exc))
        finally:
            try:
                os.remove(tmp)
            except OSError:
                pass
    return row


def run_anm(data_dir, scratch, quick):
    """Real decode check via j3_anm: walk every frame of every .anm with its
    paired .mdl. ok = files whose frames all walk; NO_MDL files cannot be walked
    (no paired .mdl) and are reported as a bucket, not as failures."""
    row = Row("anm (walk)", 92, 89,
              note="ok = PAIRED_FIT + PLAYER_48 + UNPADDED_TAIL; NO_MDL (ded1) not walkable")
    results, buckets = j3_anm.run_check(data_dir, scratch)
    row.found = len(results)
    for bk in ("PAIRED_FIT", "PLAYER_48", "UNPADDED_TAIL"):
        row.ok += len(buckets.get(bk, []))
    for d in buckets.get("UNEXPLAINED", []):
        row.bad(os.path.basename(d["name"]), d.get("why", "?"))
    row.extra = {bk: len(buckets.get(bk, [])) for bk in j3_anm.BUCKET_ORDER}
    row.extra["size_formula"] = sum(1 for _, d in results if d.get("size_formula"))
    return row


def run_adx(data_dir, jobs, quick):
    row = Row("adx decode", 372, 372)
    sdir = os.path.join(data_dir, "sound")
    afs = sorted(glob.glob(os.path.join(sdir, "demo_afs", "*.afs"))) + \
        [os.path.join(sdir, "sd.afs")]
    tasks = []     # (size, path, idx)
    for p in afs:
        f, ents = j3_audio.afs_entries(p)
        for i, e in enumerate(ents):
            if j3_audio.kind(j3_audio.afs_read(f, (e[0], min(e[1], 0x40)))) == "adx":
                tasks.append((e[1], p, i))
        f.close()
    row.found = len(tasks)
    row.extra["afs_files"] = len(afs)
    tasks.sort()
    if quick:
        tasks = tasks[:QUICK_ADX]
    row.checked = len(tasks)
    jobs_list = [(p, i) for _, p, i in tasks]
    if jobs > 1 and len(jobs_list) > 1:
        with ProcessPoolExecutor(max_workers=jobs) as ex:
            results = list(ex.map(_adx_job, jobs_list, chunksize=4))
    else:
        results = [_adx_job(t) for t in jobs_list]
    for name, why in results:
        if why is None:
            row.good()
        else:
            row.bad(name, why)
    return row


def run_movies(data_dir, jobs, quick):
    row = Row("movie .000 (--check)", 7, 7)
    files = sorted(glob.glob(os.path.join(data_dir, "movie", "*.000")),
                   key=os.path.getsize)
    row.found = len(files)
    if quick:
        files = files[:QUICK_MOVIES]
    row.checked = len(files)
    bad_sectors = 0
    if jobs > 1 and len(files) > 1:
        with ProcessPoolExecutor(max_workers=min(jobs, len(files))) as ex:
            results = list(ex.map(_movie_job, files))
    else:
        results = [_movie_job(p) for p in files]
    total_sectors = 0
    for name, tot, bad, why in results:
        total_sectors += max(tot, 0)
        if bad == 0 and why is None:
            row.good()
        else:
            bad_sectors += max(bad, 0)
            row.bad(name, why or "%d bad sectors of %d" % (bad, tot))
    row.extra["sectors"] = total_sectors
    row.extra["bad_sectors"] = bad_sectors
    return row


# --------------------------------------------------------------------- main

def fmt_table(rows, quick):
    head = "%-26s %7s %7s %7s  %-8s %s" % ("format", "found", "ok", "failed",
                                          "docs", "verdict")
    lines = [head, "-" * len(head)]
    for r in rows:
        exp = "-" if r.expected_found is None else str(r.expected_found)
        if r.expected_ok is not None and r.expected_ok != r.expected_found:
            exp += "/%d" % r.expected_ok
        ok = "n/a" if r.ok_is_na else str(r.ok)
        lines.append("%-26s %7d %7s %7d  %-8s %s" % (
            r.label, r.found, ok, len(r.fails), exp, r.verdict(quick)))
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("data_dir")
    ap.add_argument("--quick", action="store_true",
                    help="check only a sample of each format")
    ap.add_argument("--jobs", type=int, default=3,
                    help="worker processes for ADX/movies (default 3)")
    ap.add_argument("--scratch", default=DEFAULT_SCRATCH)
    args = ap.parse_args()

    data_dir = args.data_dir
    if not os.path.isfile(os.path.join(data_dir, "arc.arc")):
        print("arc.arc not found in %s (pass the 'data' directory)" % data_dir)
        return 2
    os.makedirs(args.scratch, exist_ok=True)
    t0 = time.time()
    rows = []
    timings = {}

    def timed(name, fn, *a):
        t = time.time()
        try:
            res = fn(*a)
        except Exception:
            traceback.print_exc()
            res = Row(name)
            res.bad("(checker crashed)", "see traceback")
        timings[name] = time.time() - t
        print("  [%6.1fs] %s" % (timings[name], name), flush=True)
        return res

    print("j3_check_all: data=%s mode=%s jobs=%d" %
          (data_dir, "QUICK (sample)" if args.quick else "FULL", args.jobs),
          flush=True)

    arow, erow = timed("arc validate", run_arc_validate, data_dir, args.quick)
    rows.append(arow)
    if erow:
        rows.append(erow)

    archives, _order = j3_arc.load_all(data_dir)

    rows.append(timed("map", run_generic, archives, ".map",
                      Row(".map", 287, 287), p_map, args.quick))
    rows.append(timed("cld", run_generic, archives, ".cld",
                      Row(".cld", 288, 288), p_cld, args.quick))
    cam = timed("cam", run_generic, archives, ".cam",
                Row(".cam (files)", 285, 285), p_cam, args.quick)
    rows.append(cam)
    camrec = Row(".cam (records)", 934, 934)
    camrec.found = camrec.ok = sum(CAM_RECORDS)
    rows.append(camrec)
    rows.append(timed("kg2", run_generic, archives, ".kg2",
                      Row(".kg2 (non-empty)", 232, 232), p_kg2, args.quick, True))
    rows.append(timed("lv", run_generic, archives, ".lv",
                      Row(".lv", 4, 4), p_kg2, args.quick, True))
    rows.append(timed("mdl", run_mdl, archives, args.scratch, args.quick))
    rows.append(timed("anm", run_anm, data_dir, args.scratch, args.quick))
    rows.append(timed("mes", run_generic, archives, ".mes",
                      Row(".mes (extra)", 368, 368), p_mes, args.quick))
    rows.append(timed("adx", run_adx, data_dir, args.jobs, args.quick))
    mov = timed("movies", run_movies, data_dir, args.jobs, args.quick)
    rows.append(mov)
    movb = Row("movie bad sectors", 0, None, ok_is_na=True)
    movb.found = mov.extra.get("bad_sectors", 0)
    rows.append(movb)

    if args.quick:
        # records / bad-sector rows cannot be compared on a sample
        pass

    elapsed = time.time() - t0
    print()
    print(fmt_table(rows, args.quick))
    print()
    for r in rows:
        if r.checked is not None and r.checked != r.found:
            print("note: %s -- only %d of %d checked (sample)" % (r.label, r.checked, r.found))
        if r.extra:
            print("note: %s -- %s" % (r.label, r.extra))
        if r.note:
            print("note: %s -- %s" % (r.label, r.note))
    anyfail = False
    for r in rows:
        if r.fails:
            anyfail = True
            print("\nFAILURES %s (%d total, first 5):" % (r.label, len(r.fails)))
            for n, w in r.fails[:5]:
                print("  %s  --  %s" % (n, w))
    mism = [r for r in rows if r.verdict(args.quick) == "MISMATCH"]
    print()
    if args.quick:
        print("QUICK mode: sample only; MATCH/MISMATCH not evaluated.")
    else:
        print("MISMATCH lines: %d" % len(mism))
        for r in mism:
            exp = "found=%s ok=%s" % (r.expected_found, r.expected_ok)
            print("  MISMATCH %-26s docs %s | measured found=%d ok=%s failed=%d" %
                  (r.label, exp, r.found, "n/a" if r.ok_is_na else r.ok, len(r.fails)))
    print("total runtime: %.1fs" % elapsed)

    rep = {"mode": "quick" if args.quick else "full", "runtime_s": round(elapsed, 1),
           "timings_s": {k: round(v, 1) for k, v in timings.items()},
           "rows": [dict(label=r.label, found=r.found,
                         ok=None if r.ok_is_na else r.ok,
                         failed=len(r.fails), first5=r.fails[:5],
                         docs_found=r.expected_found, docs_ok=r.expected_ok,
                         verdict=r.verdict(args.quick), extra=r.extra, note=r.note)
                    for r in rows]}
    out = os.path.join(args.scratch, "report-%s.json" % rep["mode"])
    with open(out, "w") as f:
        json.dump(rep, f, indent=1)
    print("report: %s" % out)
    return 1 if (anyfail or (mism and not args.quick)) else 0


if __name__ == "__main__":
    sys.exit(main())
