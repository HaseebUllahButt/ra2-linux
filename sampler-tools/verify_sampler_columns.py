#!/usr/bin/env python3
"""Check the hash columns of a sampler run against the raw regions.

Usage: verify_sampler_columns.py RUN_DIR

The raw region file holds the 0x410 bytes at guest 0x839218 for every frame.
This recomputes every hash column from it and compares with the columns in
frames.csv.  It also compares the Random2Class state hash (the state at
0x83921c, 1012 bytes) with the hash of the original capture.

The first four bytes of the region hold a draw-disable flag in byte 0 and the
low bytes of a process pointer in bytes 1 to 3.  They are not random state,
so a difference there is a difference between processes and not a difference
of the random number generator.
"""
import csv, hashlib, struct, sys
from pathlib import Path

reference = Path(sys.argv[2]) if len(sys.argv) > 2 else None


def load_regions(path):
    d = path.read_bytes()
    out = {}
    for i in range(0, len(d) - 1043, 1044):
        fr = struct.unpack('<I', d[i:i + 4])[0]
        out[fr] = d[i + 4:i + 1044]
    return out


def coverage(rows, raw):
    """Frame coverage of the two files of one side."""
    fr = sorted(rows)
    gaps = [f for f in range(fr[0], fr[-1] + 1) if f not in rows]
    rawfr = sorted(raw)
    rawgaps = [f for f in range(rawfr[0], rawfr[-1] + 1) if f not in raw]
    return fr, gaps, rawfr, rawgaps


def main():
    root = Path(sys.argv[1]).resolve()
    rows = {int(r['frame']): r for r in csv.DictReader((root / 'frames.csv').open())}
    reg = load_regions(root / 'rng_regions.bin')
    bad = []
    for fr, r in rows.items():
        b = reg.get(fr)
        if b is None:
            bad.append((fr, 'no raw region'))
            continue
        checks = (('sha410', b),
                  ('h839218', b[:1016]),
                  ('rng', b[4:1016]),
                  ('fixture1012', b[:1012]),
                  ('h_index', b[4:12]),
                  ('h_table', b[12:1012]),
                  ('h_tail4', b[1012:1016]))
        for col, data in checks:
            if hashlib.sha256(data).hexdigest() != r[col]:
                bad.append((fr, col))
    fr, gaps, rawfr, rawgaps = coverage(rows, reg)
    print('run: %s' % root)
    print('frames: %d, raw region records: %d' % (len(rows), len(reg)))
    print('frame rows %d..%d, gaps: %d; raw regions %d..%d, gaps: %d'
          % (fr[0], fr[-1], len(gaps), rawfr[0], rawfr[-1], len(rawgaps)))
    print('hash columns recomputed from the raw regions: %d mismatch' % len(bad))
    for x in bad[:10]:
        print('   frame %d column %s' % x)
    slots = sorted(set(r['slot'] for r in rows.values()))
    print('first word of the region (draw-disable flag and pointer bytes): %s' % slots)
    ref = reference / 'original-rng_hash.log' if reference is not None else None
    if ref is not None and ref.exists():
        orig = {}
        for line in ref.read_text().splitlines():
            p = line.split()
            if p and p[0].startswith('frame='):
                d = dict(kv.split('=', 1) for kv in p[1:] if '=' in kv)
                orig[int(p[0].split('=')[1])] = d
        same = sum(1 for f, o in orig.items()
                   if f in rows and rows[f]['rng'].startswith(o['rng']))
        ofr = sorted(orig)
        ogaps = [f for f in range(ofr[0], ofr[-1] + 1) if f not in orig]
        print('original rng_hash.log: %d frames %d..%d, gaps: %d'
              % (len(orig), ofr[0], ofr[-1], len(ogaps)))
        print('Random2Class state hash (0x83921c, 1012 bytes) equal to the original: %d of %d frames'
              % (same, len(orig)))
        print('original first word values: %s' % sorted(set(o['slot'] for o in orig.values())))
        oref = reference / 'orig_rng_regions.bin'
        if oref.exists():
            oreg = load_regions(oref)
            ofr2 = sorted(oreg)
            ogaps2 = [f for f in range(ofr2[0], ofr2[-1] + 1) if f not in oreg]
            print('original raw regions: %d records %d..%d, gaps: %d'
                  % (len(oreg), ofr2[0], ofr2[-1], len(ogaps2)))
            both = sum(1 for f in sorted(set(oreg) & set(reg))
                       if hashlib.sha256(oreg[f][4:1016]).hexdigest()
                       == hashlib.sha256(reg[f][4:1016]).hexdigest())
            print('state hash from the raw regions equal on %d of %d shared frames'
                  % (both, len(set(oreg) & set(reg))))


if __name__ == '__main__':
    main()
