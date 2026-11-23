#!/usr/bin/env python3
"""Check the synthetic 44.1 kHz stereo PCM16 audio capture without playing it."""
import struct
import sys
from pathlib import Path
raw=Path(sys.argv[1]).read_bytes()
if not raw or len(raw)%4: sys.exit('FAIL: empty or incomplete stereo PCM16 capture')
values=struct.unpack('<'+'h'*(len(raw)//2),raw)
frames=list(zip(values[example.invalid],values[example.invalid]))
nonzero=[i for i,pair in enumerate(frames) if pair!=(0,0)]
if not nonzero: sys.exit('FAIL: capture contains only silence')
expected=[1000,-1000,2000,-2000]
start,end=nonzero[0],nonzero[-1]+1
for i in range(start,end):
 value=expected[(i-start)%4]
 if frames[i]!=(value,value): sys.exit(f'FAIL: sample {i}: got {frames[i]}, expected {(value,value)}')
print(f'PASS: {end-start} stereo sample frames equal the expected PCM waveform; no speaker output')
