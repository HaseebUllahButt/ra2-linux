#!/usr/bin/env python3
import re
import sys
from pathlib import Path
text=Path(sys.argv[1]).read_text()
rows=[tuple(map(int,m)) for m in re.findall(r'^MOVIE width=(\d+) height=(\d+) frames=(\d+) decoded=(\d+) fps=(\d+)/(\d+)',text,re.M)]
if len(rows)!=4: sys.exit(f'FAIL: expected original/adapter results for two movies; got {len(rows)}')
for name,a,b in [('WESTLOGO',rows[0],rows[1]),('S13_F01E',rows[2],rows[3])]:
 if a!=b or a[2]!=a[3]: sys.exit(f'FAIL: {name}: original={a}, adapter={b}')
 print(f'MATCH {name}: {a[0]}x{a[1]}, {a[2]} decoded frames, {a[4]}/{a[5]} fps')
print('PASS: intro and mission movie frame counts and sizes match the original Bink DLL')
