#!/usr/bin/env python3
"""SH3 .mes message files (best-effort reader; format partly decoded 2026-11-11).

Layout (data/msg/<area>_<lang>_msg.mes, 368 files):
  u16 LE table T0 of n = T0[0]//2 offsets (monotonic, T0[0] = table size in bytes);
  each T0[g] points to a group record made of u16 LE text pointers (little tables
  such as dc 03 e5 03 ec 03 f8 03 00) into the text area; the text area holds the
  messages. Text bytes (English files): ASCII letters/digits; 0x00 = space,
  0x07 = apostrophe, 0x0e = line break (followed by ff ff / ff fd paging args),
  0x80 = sentence-start prefix (next letter stored minus 0x20), 0x90 = style prefix, 0xff = control escape. Other codes unknown.
Usage: j3_mes.py ARC_DIR_DATA ENTRY_PATH   e.g. j3_mes.py ".../data" data/msg/amusement_south_eng_msg.mes
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
import j3_arc

MAP = {0x00: ' ', 0x07: "'", 0x0E: '\n', 0x29: 'I'}


def decode_text(b):
    out = []
    shift = False
    for c in b:
        if shift and 0x20 <= c < 0x40:
            out.append(chr(c + 0x20))   # first letter after a 0x80 prefix is stored 0x20 low
            shift = False
            continue
        shift = (c == 0x80)
        if c in MAP:
            out.append(MAP[c])
        elif 0x20 <= c < 0x7F:
            out.append(chr(c))
        else:
            out.append('<%02x>' % c)
    return ''.join(out)


def main(argv):
    archives, _ = j3_arc.load_all(argv[1])
    a = archives['msg']
    d = a.payload(a.find(argv[2]))
    first = struct.unpack_from('<H', d, 0)[0]
    n = first // 2
    offs = struct.unpack_from('<%dH' % n, d, 0)
    print('groups', n, 'size', len(d))
    # text area = from the largest group table end; best effort: dump after the pointer tables
    start = max(offs) + 2
    # skip pointer lists (u16 values < 0x4000 and sorted) heuristically: find first printable run
    i = start
    while i < len(d) and not (0x41 <= d[i] <= 0x5A or 0x61 <= d[i] <= 0x7A):
        i += 1
    print(decode_text(d[i:]))


if __name__ == '__main__':
    main(sys.argv)
