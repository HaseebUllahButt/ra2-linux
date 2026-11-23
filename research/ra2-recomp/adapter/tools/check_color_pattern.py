#!/usr/bin/env python3
"""Compare captured 1024x768 packed RGBA data and reject empty captures."""
import sys
from pathlib import Path


def check(a_path,b_path):
    left,right=Path(a_path).read_bytes(),Path(b_path).read_bytes()
    if len(left)!=1024*768*4 or len(right)!=len(left):
        raise ValueError('Expected two 1024x768 packed RGBA captures')
    for name,data in [('Wine',left),('adapter',right)]:
        colors={data[p:p+3] for p in range(0,len(data),4)}
        if len(colors)!=65536:
            raise ValueError(f'{name} capture contains {len(colors)} colors; expected all 65536 RGB565 colors')
    count=sum(a!=b for a,b in zip(left,right))
    if count:
        pos=next(i for i,(a,b) in enumerate(zip(left,right)) if a!=b)
        raise ValueError(f'{count} changed bytes; first x={pos//4%1024}, y={pos//4//1024}, channel={pos%4}, Wine={left[pos]}, adapter={right[pos]}')
    print('PASS: all 65536 RGB565 colors match Wine across 786432 pixels')


if __name__=='__main__':
    try:
        check(*sys.argv[1:])
    except (ValueError,OSError,TypeError) as error:
        sys.exit(f'FAIL: {error}')
