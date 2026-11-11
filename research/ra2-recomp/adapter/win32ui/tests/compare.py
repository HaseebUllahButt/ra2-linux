#!/usr/bin/env python3
"""Compare each driven sequence and the complete normalized parent log."""
import difflib
import pathlib
import sys

a, b = [pathlib.Path(p).read_text().splitlines() for p in sys.argv[1:3]]
def sequences(lines):
    result = []
    current = None
    for line in lines:
        if line.startswith('sequence '):
            current = [line]
        elif current is not None:
            current.append(line)
            if line == 'end-sequence':
                result.append(current)
                current = None
    return result
x, y = sequences(a), sequences(b)
equal = sum(l == r for l, r in zip(x, y))
print(f'{equal} equal sequences; {max(len(x),len(y))-equal} different sequences')
for i, (l, r) in enumerate(zip(x, y)):
    print(('equal' if l == r else 'DIFFERENT'), i, l[0])
pathlib.Path(sys.argv[2] + '.diff').write_text('\n'.join(difflib.unified_diff(a,b,fromfile='Wine',tofile='RA2UI'))+'\n')
print('Full logs equal' if a == b else 'Full logs DIFFERENT')
sys.exit(a != b)
