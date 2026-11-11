#!/usr/bin/env python3
"""Compare corresponding packed RGBA captures. Fail if fewer than 20 frames exist.

Each folder needs frames.json: a list of objects with frame, width, height, file.
frame is a replay checkpoint ID, not an arbitrary screenshot counter.
"""
import argparse
import hashlib
import json
from pathlib import Path


def read(folder):
    frames = {}
    for entry in json.loads((folder/'frames.json').read_text()):
        frame = entry['frame']
        if frame in frames:
            raise ValueError(f'Duplicate replay checkpoint: {frame}')
        width, height = entry['width'], entry['height']
        if width <= 0 or height <= 0:
            raise ValueError('Invalid frame size')
        data = (folder/entry['file']).read_bytes()
        if len(data) != width*height*4:
            raise ValueError(f'Invalid packed RGBA byte count at {frame}')
        frames[frame] = (width, height, data)
    return frames


def compare(original, adapted, minimum):
    left, right = read(original), read(adapted)
    if set(left) != set(right):
        raise ValueError('Replay checkpoints differ')
    if len(left) < minimum:
        raise ValueError(f'Need {minimum} frames; found {len(left)}')
    for frame in sorted(left):
        w, h, a = left[frame]
        rw, rh, b = right[frame]
        if (w,h) != (rw,rh):
            raise ValueError(f'Frame {frame}: size differs')
        if a != b:
            offset = next(i for i,(x,y) in enumerate(zip(a,b)) if x != y)
            pixel = offset//4
            raise ValueError(f'Frame {frame}: first difference x={pixel%w}, y={pixel//w}, channel={offset%4}, original={a[offset]}, adapter={b[offset]}')
        print(f'MATCH {frame} {w}x{h} {hashlib.sha256(a).hexdigest()}')
    print(f'PASS: {len(left)} corresponding frames match every RGBA byte')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original',type=Path)
    parser.add_argument('adapted',type=Path)
    parser.add_argument('--minimum',type=int,default=20)
    args=parser.parse_args()
    if args.minimum<1:
        parser.error('--minimum must be positive')
    try:
        compare(args.original,args.adapted,args.minimum)
    except (ValueError,KeyError,OSError,json.JSONDecodeError) as e:
        parser.exit(1,f'FAIL: {e}\n')
