#!/usr/bin/env python3
"""Read, write and compare frame-stamped orders from Red Alert 2 1.006."""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'lib'))
import orders as O


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=('compile', 'decode', 'compare', 'inputs', 'mutate'))
    ap.add_argument('args', nargs='+')
    ap.add_argument('--types'); ap.add_argument('-o', dest='out'); ap.add_argument('--json')
    ap.add_argument('--player', type=int, default=0)
    ap.add_argument('--line', type=int); ap.add_argument('--set', nargs='*', default=[])
    a = ap.parse_args()
    if a.cmd == 'compile':
        types = O.load_types(a.types)
        recs = O.compile_script(open(a.args[0]).read().splitlines(), types)
        O.write_raw(a.out, recs, 'compiled from %s (%d records)' % (a.args[0], len(recs)))
        print('%d records -> %s' % (len(recs), a.out))
    elif a.cmd == 'decode':
        names = {}
        if a.types:
            names = {v: k for k, v in O.load_types(a.types).items()}
        for fr, e in O.read_raw(a.args[0]):
            print(fr, O.decode(e, names))
    elif a.cmd == 'compare':
        import compare
        r = compare.compare(Path(a.args[0]), Path(a.args[1]))
        if a.json:
            Path(a.json).write_text(json.dumps(r, indent=2))
        print(compare.summary(r))
        sys.exit(0 if r['pass'] else 1)
    elif a.cmd == 'inputs':
        # The game itself queues some orders: a factory's exit order for each finished unit (PLACE, id -1,
        # cell 0,0, rtti unit 1 / aircraft 2 / infantry 15) and the AI houses' orders. A replay makes them
        # again, so a recording keeps only the player's own input orders.
        keep = []
        for fr, e in O.read_raw(a.args[0]):
            if e[2] != a.player:
                continue
            if e[0] == O.EV['PLACE'] and e[7:11] in (b'\x01\0\0\0', b'\x02\0\0\0', b'\x0f\0\0\0') \
                    and e[11:15] == b'\xff\xff\xff\xff' and e[19:23] == b'\0\0\0\0':
                continue
            keep.append((fr, e))
        O.write_raw(a.out, keep, 'player %d input orders from %s' % (a.player, a.args[0]))
        print('%d input orders -> %s' % (len(keep), a.out))
    elif a.cmd == 'mutate':
        recs = O.read_raw(a.args[0]); i = a.line - 1
        fr, e = recs[i]; e = bytearray(e)
        for s in a.set:
            off, val = s.split('='); e[int(off, 0)] = int(val, 16)
        recs[i] = (fr, bytes(e))
        O.write_raw(a.out, recs, 'mutated from %s: record %d bytes %s' % (a.args[0], a.line, ' '.join(a.set)))
        print('record %d changed: %s' % (a.line, O.decode(bytes(e))))
    else:
        ap.error('unknown command ' + a.cmd)


if __name__ == '__main__':
    main()
