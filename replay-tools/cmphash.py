#!/usr/bin/env python3
"""Compare two rec.dll hash.bin files: 456-byte records per game frame
(8-byte frame number + 56 x 8-byte subsystem hashes). Prints the first
differing frame and the differing hash indices."""
import sys, struct
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
REC = 456
na, nb = len(a) // REC, len(b) // REC
n = min(na, nb)
print(f"records: ref={na} nat={nb} compared={n}")
first = None
for i in range(n):
    ra, rb = a[i*REC:(i+1)*REC], b[i*REC:(i+1)*REC]
    if ra != rb:
        first = i
        fa = struct.unpack_from('<Q', ra)[0]
        fb = struct.unpack_from('<Q', rb)[0]
        print(f"first differing record {i}: ref frame={fa} nat frame={fb}")
        ha = struct.unpack_from('<56Q', ra, 8)
        hb = struct.unpack_from('<56Q', rb, 8)
        diffs = [j for j in range(56) if ha[j] != hb[j]]
        print(f"differing hash indices ({len(diffs)}): {diffs}")
        for j in diffs[:8]:
            print(f"  idx {j}: ref {ha[j]:#018x}  nat {hb[j]:#018x}")
        break
if first is None:
    print(f"no difference in {n} records")
