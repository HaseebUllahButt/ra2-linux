#!/usr/bin/env python3
"""Compare 20 corresponding packed RGB565 frames from capture_ddraw.dll."""
import hashlib
import json
import sys
from pathlib import Path

def read(folder):
 folder=Path(folder); result={}
 for row in json.loads((folder/'frames.json').read_text()):
  key=row['frame']
  if key in result: raise ValueError('Duplicate frame number')
  if row['bits']!=16: raise ValueError('Expected RGB565 frames')
  pixels=(folder/row['file']).read_bytes()
  if len(pixels)!=row['width']*row['height']*2: raise ValueError('Incomplete surface capture')
  result[key]=(row['width'],row['height'],pixels)
 return result

def compare(a,b):
 a,b=read(a),read(b)
 if set(a)!=set(b) or len(a)!=20: raise ValueError('Need the same 20 game frame numbers')
 for frame in sorted(a):
  width,height,pixels=a[frame]; rw,rh,other=b[frame]
  if (width,height)!=(rw,rh): raise ValueError(f'Frame {frame}: dimensions differ')
  if pixels!=other:
   changed=sum(pixels[i:i+2]!=other[i:i+2] for i in range(0,len(pixels),2))
   first=next(i for i in range(0,len(pixels),2) if pixels[i:i+2]!=other[i:i+2])//2
   raise ValueError(f'Frame {frame}: {changed} changed pixels; first x={first%width}, y={first//width}')
  print(f'MATCH game frame {frame}: {width}x{height}, SHA-256 {hashlib.sha256(pixels).hexdigest()}')
 print('PASS: 20 corresponding skirmish frames match Wine pixel for pixel')
if __name__=='__main__':
 try: compare(*sys.argv[1:])
 except (ValueError,OSError,KeyError,TypeError) as e: sys.exit(f'FAIL: {e}')
