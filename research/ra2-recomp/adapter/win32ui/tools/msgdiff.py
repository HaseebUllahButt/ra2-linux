#!/usr/bin/env python3
"""Compare import recorder logs. Accept plain text or gzip. Exit 0 only for equality."""
import argparse
import collections
import gzip
import itertools
import re
from pathlib import Path

MSG = re.compile(r'\bmsg=0x([0-9a-fA-F]+)')
DEPTH = re.compile(r'\bdepth=(\d+)')
FN = re.compile(r'\bfn=(\w+)')

def base_read(path, args):
    opener = gzip.open if str(path).endswith('.gz') else open
    ignored = set()
    with opener(path, 'rt', errors='replace') as stream:
        for number, line in enumerate(stream, 1):
            line = line.rstrip('\n')
            depth = DEPTH.search(line)
            d = int(depth[1]) if depth else 0
            start = line.startswith(('CALL ', 'MSG '))
            end = line.startswith(('RET ', 'MRET '))
            message = MSG.search(line)
            fn = FN.search(line)
            if start:
                ignored = {x for x in ignored if x < d}
                if message and int(message[1], 16) in args.ignore_message:
                    ignored.add(d)
            skip = any(x <= d for x in ignored)
            if end:
                ignored.discard(d)
            if skip:
                continue
            if args.filter_input and fn and fn[1] in ('GetCursorPos', 'GetAsyncKeyState', 'GetKeyState'):
                if line.startswith('RET '):
                    line = re.sub(r'result=.*', 'result=INPUT', line)
            yield number, line

def read(path, args):
    # PeekMessage can deliver sent messages even when it returns no queued message.
    # Preserve these callbacks when removing empty polling calls.
    frames = []
    for row in base_read(path, args):
        line = row[1]
        fn = FN.search(line)
        peek = fn and fn[1] == 'PeekMessageA'
        if (args.filter_empty_peek or args.ignore_message) and peek and line.startswith('CALL '):
            frames.append([row])
            continue
        if (args.filter_empty_peek or args.ignore_message) and peek and line.startswith('RET ') and frames:
            frames[-1].append(row)
            group = frames.pop()
            m = MSG.search(line)
            drop = (args.filter_empty_peek and re.search(r'result=0(?:\s|$)', line)) or (m and int(m[1], 16) in args.ignore_message)
            group = group[1:-1] if drop else group
            if frames:
                frames[-1].extend(group)
            else:
                yield from group
        elif frames:
            frames[-1].append(row)
        else:
            yield row
    for group in frames:
        yield from group

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left', type=Path)
    parser.add_argument('right', type=Path)
    parser.add_argument('--no-filters', action='store_true')
    parser.add_argument('--keep-message', action='append', default=[], type=lambda x: int(x, 0))
    parser.add_argument('--keep-input-results', action='store_true')
    parser.add_argument('--keep-empty-peek', action='store_true')
    args = parser.parse_args()
    args.ignore_message = set() if args.no_filters else {0x113, 0x200, 0x84, 0x20} - set(args.keep_message)
    args.filter_input = not (args.no_filters or args.keep_input_results)
    args.filter_empty_peek = not (args.no_filters or args.keep_empty_peek)
    history = [collections.deque(maxlen=20), collections.deque(maxlen=20)]
    context = [[], []]
    counts = [collections.Counter(), collections.Counter()]
    first = None
    total = 0
    for index, pair in enumerate(itertools.zip_longest(read(args.left, args), read(args.right, args))):
        total = index + 1
        for side, row in enumerate(pair):
            if row and (m := MSG.search(row[1])) and not row[1].startswith(('RET ', 'MRET ')):
                counts[side][int(m[1], 16)] += 1
        differs = pair[0] is None or pair[1] is None or pair[0][1] != pair[1][1]
        if first is None and differs:
            first = index
            context = [list(h) for h in history]
        if first is not None and index <= first + 20:
            for side, row in enumerate(pair):
                context[side].append((index == first, row))
        elif first is None:
            for side, row in enumerate(pair):
                history[side].append((False, row))
    if first is None:
        print(f'EQUAL ({total} filtered lines)')
        return 0
    print(f'FIRST DIVERGENCE at filtered line {first + 1}')
    for label, path, rows in [('LEFT', args.left, context[0]), ('RIGHT', args.right, context[1])]:
        print(f'\n{label}: {path}')
        for marker, row in rows:
            if row:
                number, line = row
                print(f'{">" if marker else " "} {number}: {line}')
            elif marker:
                print('> EOF')
    a, b = counts
    print('\nMessage       LEFT      RIGHT      RIGHT-LEFT')
    for m in sorted(a.keys() | b.keys()):
        if a[m] != b[m]:
            print(f'0x{m:04x}  {a[m]:10d} {b[m]:10d} {b[m]-a[m]:+10d}')
    return 1

if __name__ == '__main__':
    raise SystemExit(main())
