"""Compare two order-replay runs (original vs JOIN, or any two runs).

PASS RULE: scenario RNG state equal at every frame boundary both runs reached, both runs reached the same
last frame, census (houses + every building/unit/infantry/aircraft row) equal at every sample, the queued
order stream (events.txt: type, house, frame and the data bytes the game uses for that type) equal, and the same outcome (final census and per-house object counts).
The screen RNG (0x839218) is reported but is not a pass rule.
"""
from pathlib import Path
from collections import defaultdict, Counter


def frames(d):
    out = {}
    for line in open(Path(d) / 'frames.txt'):
        p = line.split()
        if len(p) >= 5:
            out[int(p[0])] = (p[1], p[2], p[3], p[4], p[5] if len(p) > 5 else '')
    return out


def census(d):
    out = defaultdict(list)
    for line in open(Path(d) / 'census.txt'):
        p = line.split()
        if len(p) > 2:
            out[int(p[1])].append(line.rstrip('\n'))
    return out


# EventClass data length per type, the game's own table at 0x7D659C (what a network packet carries)
EVENT_LEN = [0, 5, 5, 4, 23, 24, 5, 5, 0, 5, 5, 16, 0, 4, 12, 12, 12, 5, 8, 0, 16, 5, 5, 4, 4, 0, 0, 1, 7, 0, 10, 4, 5,
             2, 0, 4, 4, 104, 64, 0, 4, 5, 10, 0, 5, 4]


def norm_event(line):
    """frame + type, house, frame stamp and the used data bytes; IsExecuted and unused bytes zeroed"""
    f, h = line.split()
    b = bytearray.fromhex(h)
    used = 7 + (EVENT_LEN[b[0]] if b[0] < len(EVENT_LEN) else 104)
    b[1] = 0
    b[used:] = bytes(len(b) - used)
    return f + ' ' + b.hex()


def events(d):
    return [norm_event(l) for l in open(Path(d) / 'events.txt') if l.strip()]


def outcome(rows):
    houses = {}; objs = Counter()
    for r in rows:
        p = r.split()
        if p[0] == 'H':
            houses[p[2]] = (p[3], int(p[4]))
        elif p[0] in 'BUIA':
            objs[(p[4], p[0])] += 1
    per = {h: {'country': c, 'cash': cash,
               'objects': {k: objs[(h, k)] for k in 'BUIA' if objs[(h, k)]}} for h, (c, cash) in houses.items()}
    return per


def compare(a, b):
    a, b = Path(a), Path(b)
    r = {'a': str(a), 'b': str(b)}
    fa, fb = frames(a), frames(b)
    common = sorted(set(fa) & set(fb))
    r['frames_a'], r['frames_b'] = (max(fa) if fa else 0), (max(fb) if fb else 0)
    r['common_frames'] = len(common)
    first = next((f for f in common if fa[f][:3] != fb[f][:3]), None)
    r['scenario_rng_first_difference'] = first
    if first is not None:
        r['scenario_rng_at_difference'] = {'a': fa[first][:3], 'b': fb[first][:3]}
    r['missing_frames'] = len(set(range(1, min(r['frames_a'], r['frames_b']) + 1)) - set(common))
    sfirst = next((f for f in common if fa[f][3] != fb[f][3]), None)
    r['screen_rng_first_difference_info_only'] = sfirst
    ca, cb = census(a), census(b)
    cf = sorted(set(ca) & set(cb))
    r['census_samples'] = len(cf); r['census_rows'] = sum(len(ca[f]) for f in cf)
    cdiff = None
    for f in cf:
        if ca[f] != cb[f]:
            sa, sb = set(ca[f]), set(cb[f])
            cdiff = {'frame': f, 'only_a': sorted(sa - sb)[:10], 'only_b': sorted(sb - sa)[:10],
                     'order_only': sa == sb}
            break
    r['census_first_difference'] = cdiff
    ea, eb = events(a), events(b)
    r['events_a'], r['events_b'] = len(ea), len(eb)
    edif = next((i for i in range(min(len(ea), len(eb))) if ea[i] != eb[i]), None)
    if edif is None and len(ea) != len(eb):
        edif = min(len(ea), len(eb))
    r['events_first_difference'] = None if edif is None else {
        'index': edif, 'a': ea[edif] if edif < len(ea) else None, 'b': eb[edif] if edif < len(eb) else None}
    last = max(cf) if cf else None
    r['last_census_frame'] = last
    if last is not None:
        r['outcome_a'], r['outcome_b'] = outcome(ca[last]), outcome(cb[last])
    r['same_outcome'] = last is not None and r['outcome_a'] == r['outcome_b']
    r['pass'] = (first is None and r['frames_a'] == r['frames_b'] and r['frames_a'] > 0 and r['missing_frames'] == 0
                 and cdiff is None and r['census_samples'] > 0 and r['events_first_difference'] is None
                 and r['same_outcome'])
    return r


def summary(r):
    s = ['PASS' if r['pass'] else 'FAIL',
         'frames %d / %d, %d compared, missing %d' % (r['frames_a'], r['frames_b'], r['common_frames'], r['missing_frames']),
         'scenario RNG: ' + ('equal at every boundary' if r['scenario_rng_first_difference'] is None
                             else 'first difference at frame %d %s' % (r['scenario_rng_first_difference'],
                                                                       r.get('scenario_rng_at_difference'))),
         'census: %d samples, %d rows, ' % (r['census_samples'], r['census_rows']) +
         ('equal' if r['census_first_difference'] is None else 'first difference %s' % r['census_first_difference']),
         'orders queued: %d / %d, ' % (r['events_a'], r['events_b']) +
         ('equal' if r['events_first_difference'] is None else 'first difference %s' % r['events_first_difference']),
         'outcome at frame %s: %s' % (r['last_census_frame'], 'same' if r['same_outcome'] else 'DIFFERENT'),
         'screen RNG (info only): ' + ('equal' if r['screen_rng_first_difference_info_only'] is None
                                       else 'first difference at frame %d' % r['screen_rng_first_difference_info_only'])]
    if r.get('outcome_a'):
        for h, v in sorted(r['outcome_a'].items()):
            s.append('  house %s %s cash %d objects %s' % (h, v['country'], v['cash'], v['objects']))
    return '\n'.join(s)
