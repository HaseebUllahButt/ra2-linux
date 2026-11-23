#!/usr/bin/env python3
"""Compare two JOIN in-process sampler traces, and both against the original.

Usage: compare_sampler.py RUN_A RUN_B [WINDOW_LO WINDOW_HI]

  RUN_A, RUN_B   directories with frames.csv, damage.jsonl, rng_regions.bin
  WINDOW_LO/HI   the frame window that must be complete (default 8000 9000)

Writes compare-report.txt and compare-summary.json next to run A.
"""
import json, struct, sys
from collections import Counter, defaultdict
from pathlib import Path

reference = Path(sys.argv[5]) if len(sys.argv) > 5 else None

SITE_NAME = {'0x005d3dc7': 'repair path (negative damage)',
             '0x005d3e65': 'damage (the applied hit)',
             '0x005d3f11': 'rage re-health'}


def load_frames(root):
    rows = {}
    order = []
    dup = []
    with (root / 'frames.csv').open() as f:
        f.readline()
        for line in f:
            p = line.strip().split(',')
            if len(p) < 7:
                continue
            fr = int(p[0])
            rec = dict(frame=fr, site=int(p[1]), sha410=p[2], h839218=p[3], rng=p[4],
                       fixture1012=p[5], slot=p[6])
            if len(p) > 9:
                rec['h_index'], rec['h_table'], rec['h_tail4'] = p[7], p[8], p[9]
            if fr in rows:
                dup.append(fr)
            else:
                order.append(fr)
            rows[fr] = rec
    return rows, order, dup


def load_damage(root):
    out = []
    p = root / 'damage.jsonl'
    if not p.exists():
        return out
    with p.open() as f:
        for line in f:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out


def load_regions(root):
    p = root / 'rng_regions.bin'
    out = {}
    if not p.exists():
        return out
    with p.open('rb') as f:
        while True:
            hdr = f.read(4)
            if len(hdr) < 4:
                break
            fr = struct.unpack('<I', hdr)[0]
            body = f.read(0x410)
            if len(body) < 0x410:
                break
            out[fr] = body
    return out


def load_original_rng():
    out = {}
    if reference is None: return {}
    p = reference / 'original-rng_hash.log'
    if not p.exists():
        return out
    for line in p.read_text().splitlines():
        parts = line.split()
        if not parts or not parts[0].startswith('frame='):
            continue
        d = {}
        for kv in parts[1:]:
            if '=' in kv:
                k, v = kv.split('=', 1)
                d[k] = v
        out[int(parts[0].split('=')[1])] = d
    return out


def load_original_damage():
    out = []
    if reference is None: return out
    p = reference / 'original-damage_events.jsonl'
    if not p.exists():
        return out
    with p.open() as f:
        for line in f:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out


def main():
    a_dir = Path(sys.argv[1]).resolve()
    b_dir = Path(sys.argv[2]).resolve()
    lo = int(sys.argv[3]) if len(sys.argv) > 3 else 8000
    hi = int(sys.argv[4]) if len(sys.argv) > 4 else 9000

    A, a_order, a_dup = load_frames(a_dir)
    B, b_order, b_dup = load_frames(b_dir)
    a_dmg = load_damage(a_dir)
    b_dmg = load_damage(b_dir)
    a_reg = load_regions(a_dir)
    b_reg = load_regions(b_dir)
    orig_rng = load_original_rng()
    orig_dmg = load_original_damage()

    report = {}
    lines = []

    def say(s=''):
        print(s)
        lines.append(s)

    say('JOIN in-process frame sampler: comparison of two runs and against the original')
    say('run A: %s' % a_dir)
    say('run B: %s' % b_dir)
    say('required window: frames %d..%d' % (lo, hi))
    say('')

    # ---- 1. frame coverage -------------------------------------------------
    say('1. Frame coverage')
    cover = {}
    for name, rows, order, dup in (('A', A, a_order, a_dup), ('B', B, b_order, b_dup)):
        missing = [f for f in range(lo, hi + 1) if f not in rows]
        inwin = [f for f in order if lo <= f <= hi]
        expect = list(range(lo, hi + 1))
        say('   run %s: %d frame rows in total, %d rows in the window %d..%d, %d missing, %d repeated'
            % (name, len(rows), len(inwin), lo, hi, len(missing), len(dup)))
        say('      window frames are present once each and in increasing order: %s'
            % ('yes' if inwin == expect else 'NO'))
        say('      frame boundary sites used: %s' % sorted(set(r['site'] for r in rows.values())))
        cover[name] = dict(rows=len(rows), window_rows=len(inwin), missing=len(missing),
                           repeated=len(dup), complete=(inwin == expect))
    report['coverage'] = cover
    say('')

    # ---- 2. run A against run B ------------------------------------------
    say('2. Repeat control: run A against run B')
    common = sorted(set(A) & set(B))
    fields = ['rng', 'sha410', 'h_index', 'h_table', 'h_tail4']
    diffs = {f: [x for x in common if A[x].get(f) != B[x].get(f)] for f in fields}
    say('   common frames: %d (only in A %d, only in B %d)'
        % (len(common), len(set(A) - set(B)), len(set(B) - set(A))))
    for f, label in (('rng', 'state only (bytes 4..1015 of the region)'),
                     ('sha410', 'the whole 0x410 byte region'),
                     ('h_index', 'the two index words'),
                     ('h_table', 'the 250 word random table'),
                     ('h_tail4', 'the four bytes at 1012')):
        d = diffs[f]
        if f in A[common[0]]:
            say('   %-52s equal on %d of %d frames, different on %d'
                % (label, len(common) - len(d), len(common), len(d)))
            if d:
                say('      first differing frame: %d' % d[0])
    first_any = min([d[0] for d in diffs.values() if d] or [None])
    say('   first frame where any part of the region differs: %s' % first_any)
    report['repeat'] = dict(common=len(common),
                            only_a=len(set(A) - set(B)), only_b=len(set(B) - set(A)),
                            differ={f: len(diffs[f]) for f in fields},
                            first_difference=first_any,
                            first_difference_frames={f: diffs[f][:40] for f in fields})

    # raw bytes: which offsets differ
    if a_reg and b_reg:
        offsets = Counter()
        per_frame = {}
        for fr in sorted(set(a_reg) & set(b_reg)):
            x, y = a_reg[fr], b_reg[fr]
            bad = [i for i in range(0x410) if x[i] != y[i]]
            if bad:
                per_frame[fr] = bad
                for i in bad:
                    offsets[i] += 1
        if per_frame:
            say('   raw region: %d of %d frames differ, at these byte offsets inside the region:'
                % (len(per_frame), len(set(a_reg) & set(b_reg))))
            groups = []
            for off in sorted(offsets):
                groups.append('0x%04x(%d)' % (off, offsets[off]))
            say('      %s' % ', '.join(groups[:60]))
            f0 = min(per_frame)
            say('      first differing frame %d, differing offsets: %s'
                % (f0, [hex(o) for o in per_frame[f0]][:20]))
            report['repeat']['raw_diff_frames'] = len(per_frame)
            report['repeat']['raw_diff_offsets'] = {hex(o): c for o, c in sorted(offsets.items())}
        else:
            say('   raw region: every frame is byte for byte equal')
            report['repeat']['raw_diff_frames'] = 0
    say('')

    # ---- 3. damage events --------------------------------------------------
    say('3. Damage events')
    say('   run A: %d events, run B: %d events' % (len(a_dmg), len(b_dmg)))
    k = lambda e: (e['frame'], e.get('target_uid'), e.get('damage'), e.get('health'),
                    e.get('attacker_uid'))
    ka, kb = [k(e) for e in a_dmg], [k(e) for e in b_dmg]
    if ka == kb:
        say('   the two event lists are identical (frame, target, damage, health, attacker)')
    else:
        n = min(len(ka), len(kb))
        first = next((i for i in range(n) if ka[i] != kb[i]), n)
        say('   first differing event index %d of %d' % (first, max(len(ka), len(kb))))
    say('   events per health-write site: %s' % dict(Counter(e['site'] for e in a_dmg)))
    for s, n in sorted(Counter(e['site'] for e in a_dmg).items()):
        say('      %s: %d events  (%s)' % (s, n, SITE_NAME.get(s, '')))
    hits = [e for e in a_dmg if e['site'] == '0x005d3e65']
    say('   damage events (site 0x005d3e65): %d, of which %d inside the window %d..%d'
        % (len(hits), len([e for e in hits if lo <= e['frame'] <= hi]), lo, hi))
    say('   first and last damage frame: %d and %d'
        % (min(e['frame'] for e in a_dmg), max(e['frame'] for e in a_dmg)))
    report['damage'] = dict(a_events=len(a_dmg), b_events=len(b_dmg), identical=ka == kb,
                            per_site=dict(Counter(e['site'] for e in a_dmg)),
                            window_events=len([e for e in hits if lo <= e['frame'] <= hi]))

    # ---- 4. against the original ------------------------------------------
    say('')
    say('4. Against the original game (Windows original, same map and seed)')
    if orig_rng:
        say('   original random-state rows: %d (frames %d..%d)'
            % (len(orig_rng), min(orig_rng), max(orig_rng)))
        say('   the original region starts with a process pointer, so only the state bytes')
        say('   (offset 4..1015) can be compared across processes.')
        rows = []
        for off in range(-3, 4):
            hit = tot = 0
            for f, r in orig_rng.items():
                j = f + off
                if j in A:
                    tot += 1
                    if A[j]['rng'] == r['rng']:
                        hit += 1
            rows.append((off, hit, tot))
            say('      offset %+d: %d of %d frames equal (%.1f%%)'
                % (off, hit, tot, 100.0 * hit / max(tot, 1)))
        best = max(rows, key=lambda x: x[1])
        say('   best offset %+d with %d equal frames' % (best[0], best[1]))
        report['original_rng'] = dict(rows=len(orig_rng),
                                      offsets=[dict(offset=o, equal=h, compared=t) for o, h, t in rows],
                                      best_offset=best[0], best_equal=best[1])

    if orig_dmg:
        hp = [e for e in orig_dmg if e.get('event') == 'hp_change']
        say('   original damage rows: %d total, %d of them health changes' % (len(orig_dmg), len(hp)))
        # group the JOIN events per frame and target
        grouped = defaultdict(list)
        for e in hits:
            grouped[(e['frame'], e['target_uid'])].append(e)
        # the original reports a health change in the frame after the hit:
        # its sample is taken just after the frame counter moved on
        for shift, tag in ((1, 'JOIN frame + 1'), (0, 'JOIN frame')):
            hit = tot = 0
            firstmiss = None
            for e in hp:
                key = (e['frame'] - shift, e['uid'])
                if key in grouped:
                    tot += 1
                    evs = grouped[key]
                    before = evs[0]['old_health']
                    after = evs[-1]['health']
                    if before == e['hp_before'] and after == e['hp_after']:
                        hit += 1
                    elif firstmiss is None:
                        firstmiss = (e, evs)
            say('   health changes that match the JOIN damage events at %s: %d of %d'
                % (tag, hit, tot))
            if firstmiss:
                e, evs = firstmiss
                say('      first mismatch: original frame %d uid %d %d -> %d; JOIN %s'
                    % (e['frame'], e['uid'], e['hp_before'], e['hp_after'],
                       [(x['frame'], x['old_health'], x['health'], x['damage'], x['attacker_uid']) for x in evs]))
        # attacker attribution: where the original names one attacker, does JOIN name the same?
        agree = disagree = 0
        detail = []
        for e in hp:
            key = (e['frame'] - 1, e['uid'])
            if key in grouped and e.get('unique_attacker') and len(e.get('attackers') or []) == 1:
                ju = grouped[key][0]['attacker_uid']
                ou = e['attackers'][0]['uid']
                if ju == ou:
                    agree += 1
                else:
                    disagree += 1
                    detail.append((e['frame'], e['uid'], ou, ju))
        say('   attacker attribution where the original had exactly one candidate attacker:')
        say('      JOIN names the same attacker in %d cases, a different one in %d cases'
            % (agree, disagree))
        for d in detail[:10]:
            say('      frame %d victim uid %d: original candidate %d, JOIN actual %d' % d)
        report['original_damage'] = dict(rows=len(orig_dmg), hp_changes=len(hp),
                                         attacker_agree=agree, attacker_disagree=disagree)
    say('')
    out = a_dir / 'compare-summary.json'
    out.write_text(json.dumps(report, indent=2) + '\n')
    (a_dir / 'compare-report.txt').write_text('\n'.join(lines) + '\n')
    print('summary written to %s' % out)


if __name__ == '__main__':
    main()
