#!/usr/bin/env python3
"""Compare the computer opponent of the ORIGINAL game with our rebuild.

Usage: compare_census.py ORIGINAL_LOG NATIVE_LOG [--country Russians] [--brief]

ORIGINAL_LOG: census.log from ~/Projects/ra2-oracle/bin/orig_census.py
  (CENSUS sec=S [frame=F] house=... types=A:1,B:2).
NATIVE_LOG: stderr of build/ra2-native with RA2_HOUSE_CENSUS=1
  (CENSUS frame=F house=... types=A:1,B:2).

Game time: if both logs have frame=, both use frame / 20.7 / 60 minutes.
Otherwise the original uses its real seconds (speed 3 runs at about 20.7
frames per second) and the native log uses frame / 20.7.
If the original log holds more than one run, the last run is used.
"""
import argparse, os, re, sys

FPS = 20.7
MINUTES = (5, 10, 15, 20, 25, 30)
FIELDS = ('structures', 'vehicles', 'infantry', 'miners', 'cash')
HERE = os.path.dirname(os.path.abspath(__file__))
# Only the [BuildingTypes] ID list is read, to tell buildings from units.
# The list comes from the base RA2 RULES.INI, extracted by make build/rules.ini.
RULES = os.path.join(HERE, '..', 'build', 'rules.ini')


def building_ids(path):
    ids, inside = set(), False
    try:
        for line in open(path, encoding='latin-1'):
            line = line.split(';', 1)[0].strip()
            if line.startswith('['):
                inside = line.lower() == '[buildingtypes]'
            elif inside and '=' in line:
                ids.add(line.split('=', 1)[1].strip().upper())
    except OSError:
        print(f'missing building list {path}: run make build/rules.ini', file=sys.stderr)
        sys.exit(2)
    if not ids:
        print(f'missing building list {path}: run make build/rules.ini', file=sys.stderr)
        sys.exit(2)
    return ids


def parse(path):
    """Return the CENSUS samples of the last run in the log."""
    runs = [[]]
    for line in open(path, errors='replace'):
        if line.startswith('# census pid='):
            runs.append([])
        if not line.startswith('CENSUS '):
            continue
        s = {}
        for part in line.split()[1:]:
            if '=' not in part:
                continue
            k, v = part.split('=', 1)
            if k == 'types':
                s[k] = {t: int(c) for t, c in (x.rsplit(':', 1) for x in v.split(',') if ':' in x)}
            elif k == 'country':
                s[k] = v
            else:
                try:
                    s[k] = int(float(v))
                except ValueError:
                    s[k] = v
        s.setdefault('types', {})
        runs[-1].append(s)
    runs = [r for r in runs if r]
    return runs[-1] if runs else []


def house_samples(samples, country):
    houses = sorted({s['house'] for s in samples if s.get('country') == country})
    if not houses:
        return []
    return [s for s in samples if s['house'] == houses[0]]


def set_minutes(samples, use_frames):
    for s in samples:
        if use_frames or 'sec' not in s:
            s['min'] = s['frame'] / FPS / 60.0
        else:
            s['min'] = s['sec'] / 60.0


def at(samples, minute, slack=0.75):
    best = None
    for s in samples:
        d = abs(s['min'] - minute)
        if d <= slack and (best is None or d < abs(best['min'] - minute)):
            best = s
    return best


def first_and_max(samples, kind_ids, want_building):
    out = {}
    for s in samples:
        for t, c in s['types'].items():
            if (t.upper() in kind_ids) != want_building or c <= 0:
                continue
            first, peak = out.get(t, (None, 0))
            out[t] = (s['min'] if first is None else first, max(peak, c))
    return out


def rel(a, b):
    return abs(a - b) / max(abs(a), abs(b), 1)


def fmt_min(v):
    return '-' if v is None else f'{v:.1f}'


def compare(orig_path, native_path, country, top, brief, rules):
    bids = building_ids(rules)
    orig_all, nat_all = parse(orig_path), parse(native_path)
    use_frames = bool(orig_all) and all('frame' in s for s in orig_all) and all('frame' in s for s in nat_all)
    orig, nat = house_samples(orig_all, country), house_samples(nat_all, country)
    if not orig or not nat:
        print(f'no {country} house in {"original" if not orig else "native"} log', file=sys.stderr)
        return 2
    set_minutes(orig, use_frames)
    set_minutes(nat, use_frames)
    end = min(orig[-1]['min'], nat[-1]['min'])
    # The original game ends when its human house (Americans) is beaten:
    # no structures and no vehicles or infantry left, after it had structures.
    defeat = None
    had_structures = False
    set_minutes(orig_all, use_frames)
    for s in orig_all:
        if s.get('country') != 'Americans':
            continue
        if s.get('structures', 0) > 0:
            had_structures = True
        elif had_structures and s.get('vehicles', 0) + s.get('infantry', 0) == 0:
            defeat = s['min']
            break
    # The original game also ends when it freezes: from one sample on, no
    # house changes for 3 minutes or more until the log ends (the defeat
    # screen stops the game).
    groups = []  # one per minute: [key, first min, last min]
    for s in orig_all:
        m = s['min']
        if groups and groups[-1][2] == m:
            groups[-1][3].append(s)
            continue
        groups.append([None, m, m, [s]])
    for g in groups:
        g[0] = tuple(sorted((k, repr(v)) for s in g[3]
                            for k, v in s.items()
                            if k not in ('sec', 'frame', 'min') and not k.startswith('new_')))
    freeze = None
    for i in range(len(groups) - 1, -1, -1):
        if groups[i][0] != groups[-1][0]:
            break
        freeze = groups[i][1]
    if freeze is not None and groups[-1][2] - freeze >= 3.0:
        defeat = freeze if defeat is None else min(defeat, freeze)
    if defeat is not None:
        end = min(end, defeat)
    diffs = []  # (score, text)

    rows = []
    for m in MINUTES:
        o, n = at(orig, m), at(nat, m)
        rows.append((m, o, n))
        if m > end or not o or not n:
            continue
        for f in FIELDS:
            a, b = o.get(f, 0), n.get(f, 0)
            if a == b:
                continue
            # Cash moves fast; give it half weight.
            score = rel(a, b) * (0.5 if f == 'cash' else 1.0) * (min(abs(a - b), 5) / 5 if f != 'cash' else 1)
            diffs.append((score, f'{f} at {m} min: original {a}, ours {b} ({b - a:+d})'))

    ob, nb = first_and_max(orig, bids, True), first_and_max(nat, bids, True)
    ou, nu = first_and_max(orig, bids, False), first_and_max(nat, bids, False)
    for kind, oo, nn in (('building', ob, nb), ('unit', ou, nu)):
        weight = 1.0 if kind == 'building' else 0.8
        for t in sorted(set(oo) | set(nn)):
            of, om = oo.get(t, (None, 0))
            nf, nm = nn.get(t, (None, 0))
            if of is None and nf is not None and nf <= end:
                diffs.append((weight * (1 + min(nm, 5) / 5), f'{kind} {t}: ours builds it (first {nf:.1f} min, max {nm}); original never'))
            elif nf is None and of is not None and of <= end:
                diffs.append((weight * (1 + min(om, 5) / 5), f'{kind} {t}: original builds it (first {of:.1f} min, max {om}); ours never'))
            elif of is not None and nf is not None:
                dt = abs(nf - of)
                if dt >= 1.5:
                    diffs.append((weight * min(dt / 10, 1.0), f'{kind} {t}: first at {of:.1f} min in original, {nf:.1f} min in ours ({nf - of:+.1f})'))
                if om != nm:
                    diffs.append((weight * rel(om, nm) * min(abs(om - nm), 5) / 5,
                                  f'{kind} {t}: max count original {om}, ours {nm} ({nm - om:+d})'))
    diffs.sort(key=lambda x: -x[0])

    if brief:
        cells = []
        for m, o, n in rows:
            if m in (10, 20, 30):
                if m > end:
                    cells.append(f'st{m} -/-')
                else:
                    cells.append(f'st{m} {o["structures"] if o else "-"}/{n["structures"] if n else "-"}')
        last = min(30, end)
        last_o, last_n = at(orig, last) or orig[-1], at(nat, last) or nat[-1]
        cells.append(f'veh {last_o["vehicles"]}/{last_n["vehicles"]}')
        cells.append(f'inf {last_o["infantry"]}/{last_n["infantry"]}')
        cells.append(f'min {last_o["miners"]}/{last_n["miners"]}')
        tail = f'  (orig game end {defeat:.1f} min)' if defeat is not None else ''
        print('  '.join(cells) + ('  | ' + diffs[0][1] if diffs else '') + tail)
        return 0

    print(f'Computer house {country}: original {orig_path} vs ours {native_path}')
    print(f'Time base: {"game frames in both logs" if use_frames else "original real seconds, native frames / 20.7"};'
          f' compared up to {end:.1f} min')
    if defeat is not None:
        print(f'Original human defeated at {defeat:.1f} min; compared up to {defeat:.1f} min')
    print()
    print('min  ' + ''.join(f'{f:>20}' for f in FIELDS))
    print('     ' + ''.join(f'{"orig / ours":>20}' for _ in FIELDS))
    for m, o, n in rows:
        if m > end:
            print(f'{m:<5}' + ''.join(f'{"-":>20}' for _ in FIELDS))
            continue
        cells = []
        for f in FIELDS:
            a = str(o[f]) if o and f in o else '-'
            b = str(n[f]) if n and f in n else '-'
            cells.append(f'{a + " / " + b:>20}')
        print(f'{m:<5}' + ''.join(cells))
    print()
    print(f'{"building type":<14}{"first min orig":>16}{"first min ours":>16}{"max orig":>10}{"max ours":>10}')
    for t in sorted(set(ob) | set(nb), key=lambda t: (min(x for x in (ob.get(t, (None,))[0], nb.get(t, (None,))[0]) if x is not None), t)):
        of, om = ob.get(t, (None, 0))
        nf, nm = nb.get(t, (None, 0))
        print(f'{t:<14}{fmt_min(of):>16}{fmt_min(nf):>16}{om:>10}{nm:>10}')
    print()
    print(f'{"unit type":<14}{"first min orig":>16}{"first min ours":>16}{"max orig":>10}{"max ours":>10}')
    for t in sorted(set(ou) | set(nu)):
        of, om = ou.get(t, (None, 0))
        nf, nm = nu.get(t, (None, 0))
        print(f'{t:<14}{fmt_min(of):>16}{fmt_min(nf):>16}{om:>10}{nm:>10}')
    print()
    print('Largest differences first:')
    for score, text in diffs[:top]:
        print(f'  {score:4.2f}  {text}')
    if not diffs:
        print('  none')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('original_log')
    ap.add_argument('native_log')
    ap.add_argument('--country', default='Russians', help='country of the computer house (default Russians)')
    ap.add_argument('--top', type=int, default=15, help='number of differences to list')
    ap.add_argument('--brief', action='store_true', help='one summary line')
    ap.add_argument('--rules', default=RULES, help='INI file with the [BuildingTypes] list')
    a = ap.parse_args()
    sys.exit(compare(a.original_log, a.native_log, a.country, a.top, a.brief, a.rules))


if __name__ == '__main__':
    main()
