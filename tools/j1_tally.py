"""J1 tally: classify every rt_unimpl site in port/gen-sh3/ by kind and provenance.

Provenance: a site is "ghidra" if its address appears in the body of a function
exported by Ghidra (re/out/functions-sh3.json); otherwise it was discovered by
the recompiler's recursive descent or pointer scan (could be real code Ghidra
missed, or misdiscovered data).

Kind comes from the recompiler's own report (build/x86rec-sh3.log):
"simd:<mn>", "unhandled:<mn>", "x87 ...". int3 sites are padding.

Codec range widened 2026-11-11: [0x64c000, 0x670000) — the 0x64c000-0x64ffff block is
self-contained (only called from the codec itself), holds the CRI WaveSDK DSP (SSE +
3DNow!), and is destined for native decoders (J5) like [0x650000, 0x670000).

Usage: python3 tools/j1_tally.py [log]
"""
import json, pathlib, re, collections, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GEN = ROOT / "port" / "gen-sh3"
FUNCS = ROOT / "re" / "out" / "functions-sh3.json"
LOG = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "x86rec-sh3.log"
CODEC_LO, CODEC_HI = 0x64c000, 0x670000

SITE = re.compile(r'rt_unimpl\(c, (0x[0-9a-f]+)u?, "([^"]+)"\)')

# mnemonic -> kind, from the recompiler report lines "  N  simd:movq   ex: ..."
kind_of = {}
for line in LOG.read_text().splitlines():
    m = re.match(r"\s+\d+\s+(simd|unhandled|x87)[: ](\S+)", line)
    if m:
        kind_of[m.group(2)] = m.group(1)

def kind(reason):
    if reason == "int3":
        return "int3-padding"
    base = reason.split()[0]
    return kind_of.get(base, kind_of.get(reason, "scalar"))

# Ghidra-exported instruction addresses
ghidra = set()
for p in json.loads(FUNCS.read_text(encoding="utf-8-sig")):
    ghidra.update(p["insns"])

rows = []
for f in sorted(GEN.glob("x86_f*.c")) + [GEN / "x86_thunks.c"]:
    for m in SITE.finditer(f.read_text()):
        rows.append((int(m.group(1), 16), m.group(2), f.name))

buckets = collections.Counter()
simd_out = collections.Counter()
scalar_out = collections.Counter()
x87_out = collections.Counter()
form_out = collections.Counter()
for a, reason, fname in rows:
    k = kind(reason)
    prov = "ghidra" if a in ghidra else "descent"
    zone = "codec" if CODEC_LO <= a < CODEC_HI else "outside"
    buckets[(k, zone, prov)] += 1
    if zone == "outside":
        if k == "simd":
            simd_out[reason] += 1
        elif k == "unhandled":
            scalar_out[reason] += 1
        elif k == "x87":
            x87_out[reason] += 1
        elif k == "int3-padding":
            form_out["int3"] += 1

print(f"total sites: {len(rows)}")
print(f"{'kind':16}{'zone':9}{'provenance':11}count")
for (k, z, p), n in sorted(buckets.items(), key=lambda x: -x[1]):
    print(f"{k:16}{z:9}{p:11}{n}")

print("\nSIMD mnemonics OUTSIDE codec range:")
for r, n in simd_out.most_common():
    print(f"  {r:16}{n}")
print("\nscalar gaps OUTSIDE codec range:")
for r, n in scalar_out.most_common():
    print(f"  {r:16}{n}")
print("\nx87 gaps OUTSIDE codec range:")
for r, n in x87_out.most_common():
    print(f"  {r:16}{n}")
print("\nother OUTSIDE codec range:")
for r, n in form_out.most_common():
    print(f"  {r:16}{n}")
