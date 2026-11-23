#!/usr/bin/env python3
"""Check census fields only at identical frame numbers, past frame 3000."""
import json,sys
from pathlib import Path
FIELDS=('country','cash','structures','vehicles','infantry','aircraft','miners','types')
def load(path):
    frames={}
    for line in Path(path).read_text().splitlines():
        if not line.startswith('CENSUS '):continue
        row=dict(v.split('=',1) for v in line.split()[1:] if '=' in v)
        frames.setdefault(int(row['frame']),{})[int(row['house'])]={k:row.get(k,'') for k in FIELDS}
    return frames
original=load(sys.argv[1]);native=load(sys.argv[2]);common=sorted(original.keys()&native.keys())
bad=[f for f in common if original[f]!=native[f]]
result={'equal_frame_pairs':len(common),'identical':len(common)-len(bad),'different_frames':bad,'last_compared_frame':max(common,default=-1),'last_native_frame':max(native,default=-1)}
print(json.dumps(result,indent=2))
raise SystemExit(bool(bad) or result['last_compared_frame']<=3000)
