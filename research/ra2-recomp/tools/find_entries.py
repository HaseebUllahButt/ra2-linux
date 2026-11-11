#!/usr/bin/env python3
"""find_entries.py: grow a function-entry list until it covers the code of game.exe.
usage: find_entries.py <old-list> <new-list> [log.tsv] [--seed TAG=FILE]...
  --seed: extra start addresses (hex, one per line) from another tool; TAG names the source.
  Tags 'seed' and 'rtti' are trusted starts (they win conflicts); other tags rank like 'pad'.

Candidates are only taken in bytes that no listed function reaches and that are
not padding (int3 / nop forms / zero runs):
  call   direct call target from discovered code
  ptr    aligned 32-bit value in .rdata/.data, or an immediate in code, that points into .text
  pad    first byte after padding (or after the end of reached code) in an uncovered area
  noret  byte after a call to a function that never returns, when no jump goes there
         (the caller's discovery ran on into the next function)
  TAG    --seed lists; these may also sit inside reached code, at an instruction start that is
         only reached by a jump, a ret, or a call that never returns
Each candidate is discovered with lift.Func.discover(); all other candidates count as
entries, so a function stops where the next one starts. A candidate is dropped when:
  - discovery hits bytes that do not decode, or odd instructions (in/out, les, ...),
  - it starts like data (add [reg],reg forms = small byte values),
  - its instructions start in the middle of another function's instruction,
  - it runs over jump-table bytes or bytes that code reads as data,
  - it runs into a listed function without a call or jump (a stub or padding),
  - another new function runs into it without a call: it is a label of that function,
    unless it is a call target or an aligned start after padding (then the other one is dropped).
Listed code never reaches uncovered bytes, so the old functions do not change, except
where a noret/seed entry cuts a function after a call that never returns.
Rounds repeat until nothing new is found; then all new entries are checked together
once more (a later round can find the real start in front of an earlier label).
log.tsv: address, source, verdict, reason."""
import bisect, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lift as L
import coverage as C

PRIO = {'seed': 0, 'rtti': 0, 'call': 0, 'noret': 1, 'pad': 1, 'ptr': 2}
TRUSTED = ('seed', 'rtti', 'call')

def code_areas(img, cov):
    """Non-padding parts of the uncovered gaps, sorted."""
    out = []
    for s, e in cov.gaps():
        _, cores = C.split_padding(img, s, e)
        out.extend(cores)
    return out

def noreturn(res):
    """Functions whose discovery has no way back to the caller."""
    return {a for a, r in res.items() if r['ok'] and r['insns'] and not r['returns']}

class Flow:
    """How each reached instruction start is entered: by plain fall-through, after a call, or by a jump."""
    def __init__(self, img, res):
        self.plain, self.after_call, self.labels = set(), {}, set()
        for r in res.values():
            for e, t in r['callsites']:
                self.after_call.setdefault(e, set()).add(t)
            self.labels.update(r['labels'])
        for r in res.values():
            for x, s in r['insns']:
                n = x + s
                if n in self.after_call:
                    continue
                b = img.read(x, 2)
                if b[0] in (0xc3, 0xc2, 0xe9, 0xeb, 0xcc):          # ret, jmp, int3: no fall-through
                    continue
                if b[0] == 0xff and (b[1] >> 3) & 7 in (4, 5):      # jmp [..]
                    continue
                if b[0] == 0xff and (b[1] >> 3) & 7 in (2, 3):      # call [..]: may return
                    pass
                self.plain.add(n)

def candidates(img, cov, dptr, entries, areas, extra, flow, noret):
    starts = [s for s, e in areas]
    out = {}
    def add(t, src):
        if t in entries or not (cov.lo <= t < cov.hi):
            return
        i = bisect.bisect_right(starts, t) - 1
        if i >= 0 and areas[i][0] <= t < areas[i][1]:
            out.setdefault(t, set()).add(src)
    for t in cov.calls: add(t, 'call')
    for t in cov.ptrs: add(t, 'ptr')
    for t in dptr: add(t, 'ptr')
    for s, e in areas:
        add(s, 'pad')
    def inside(t, src, need_after_call):
        """t is an instruction start of reached code: allowed when nothing falls into it plainly
        and every direct call right before it never returns."""
        if t in entries or not (cov.lo <= t < cov.hi) or cov.m[t - cov.lo] != 1 or not cov.start[t - cov.lo]:
            return
        if t in flow.plain or img.u8(t) in (0xcc, 0x90, 0x00):
            return
        if any(c not in noret for c in flow.after_call.get(t, ())):
            return
        if need_after_call and (t in flow.labels or t not in flow.after_call):
            return
        out.setdefault(t, set()).add(src)
    for tag, ts in extra.items():
        for t in ts:
            add(t, tag)
            inside(t, tag, False)
    for e in flow.after_call:
        inside(e, 'noret', True)
    return out

def data_like(img, r):
    """Small byte values decode as add [reg],reg, and tables repeat one instruction:
    a function does not start like that."""
    first = sorted(r['insns'])[:4]
    for x, s in first[:3]:
        b = img.read(x, 2)
        if b[0] <= 3 and b[1] < 0x40:
            return x
    bs = [img.read(x, s) for x, s in first]
    for k in range(len(bs) - 2):
        if bs[k] == bs[k + 1] == bs[k + 2]:
            return first[k][0]
    return None

def real_start(img, x, src):
    """A call target, or an aligned address right after padding."""
    return 'call' in src or (x % 16 == 0 and img.u8(x - 1) in (0xcc, 0x90))

def owner_at(cov, k):
    """Listed function whose instruction covers byte k."""
    for x in range(k, k - 16, -1):
        if x in cov.owner:
            return cov.owner[x]
    return None

def check(img, r, cov, src, entries, bogus=None):
    """Why candidate result r is bad on its own, or None.
    bogus: dict to collect listed entries that start inside one of this candidate's instructions
    (a strong candidate wins over such an entry; the entry is reported, not removed)."""
    lo = cov.lo
    if not r['ok']:
        return r['error']
    strong = real_start(img, r['entry'], src) or any(s in TRUSTED for s in src)
    for a, why in r['bad'].items():
        if why == 'cannot decode':
            return 'cannot decode at %08x' % a
        if why == 'falls into next function' and a in entries:
            return 'runs into listed function %08x without a call' % a
    if r['odd'] and not strong:
        return 'odd instruction at %08x' % r['odd'][0]
    ins = r['insns']
    if not ins:
        return 'empty'
    d = data_like(img, r)
    if d is not None:
        return 'looks like data at %08x' % d
    inner = set()
    for x, s in ins:
        inner.update(range(x + 1, x + s))
    found = {}
    for x, s in ins:
        if not (cov.lo <= x and x + s <= cov.hi):
            return 'runs out of .text'
        for k in range(x, x + s):
            m = cov.m[k - lo]
            if m == 2:
                return 'runs over jump table at %08x' % k
            if m == 1 and (k == x) != bool(cov.start[k - lo]):
                o = owner_at(cov, k) if k != x else None
                if k == x:
                    # our instruction starts inside a listed instruction: find who owns it
                    o = owner_at(cov, k - 1)
                if strong and o is not None and o in inner and o not in cov.calls and not real_start(img, o, ()):
                    found[o] = r['entry']
                    continue
                return 'splits an instruction of listed code at %08x' % k
            if k in cov.refs:
                return 'code reads %08x as data' % k
    if src == {'pad'} and not strong and sum(s for x, s in ins) < 3:
        return 'too small for a function'
    if bogus is not None:
        bogus.update(found)
    return None

def settle(img, live, entries, cov, base, bogus=None):
    """Discover candidates together and drop bad ones until stable. Returns (kept, rejected, results)."""
    rejected = {}
    while live:
        cres = C.discover_all(entries | set(live), live)
        order = sorted(live, key=lambda a: (min(PRIO.get(x, 1) for x in live[a]), a))
        starts, inner = set(), set()
        bad = {}
        for a in order:
            r = cres[a]
            why = check(img, r, cov, live[a], base)
            if why is None:
                for x, s in r['insns']:
                    if x in inner:
                        why = 'splits an instruction of new code at %08x' % x; break
                    if any(k in starts for k in range(x + 1, x + s)):
                        why = 'splits an instruction of new code near %08x' % x; break
            if why is None:
                for x, s in r['insns']:
                    starts.add(x); inner.update(range(x + 1, x + s))
            else:
                bad[a] = why
        # plain fall-through from another new function into a candidate: it is a label, not a start
        for a in order:
            if a in bad:
                continue
            for x, why in cres[a]['bad'].items():
                if why == 'falls into next function' and x in live and x not in bad:
                    if real_start(img, x, live[x]):
                        bad[a] = 'runs into function %08x without a call' % x
                        break
                    bad[x] = 'label: function %08x runs into it' % a
        if not bad:
            if bogus is not None:
                for a in live:
                    check(img, cres[a], cov, live[a], base, bogus)
            return live, rejected, cres
        for a, why in bad.items():
            rejected[a] = why
            del live[a]
    return live, rejected, {}

def main():
    args, extra = [], {}
    av = sys.argv[1:]
    while av:
        a = av.pop(0)
        if a == '--seed':
            tag, fn = av.pop(0).split('=', 1)
            extra.setdefault(tag, set()).update(C.load_entries(fn))
        else:
            args.append(a)
    old, new = args[0], args[1]
    logp = args[2] if len(args) > 2 else None
    img = L.Image(L.EXE)
    base = C.load_entries(old)
    dptr = C.data_pointers(img)
    base_res = C.discover_all(base)
    base_cov = C.Cover(img, base_res)
    added = {}      # addr -> sources
    log, rejected = {}, {}
    res = dict(base_res)
    entries = set(base)
    rnd = 0
    while True:
        rnd += 1
        cov = C.Cover(img, res)
        areas = code_areas(img, cov)
        flow = Flow(img, res)
        cand = {a: s for a, s in candidates(img, cov, dptr, entries, areas, extra, flow, noreturn(res)).items()
                if a not in rejected}
        if cand:
            live, rej, cres = settle(img, dict(cand), entries, cov, base)
            for a, why in rej.items():
                rejected[a] = why
                log[a] = (cand[a], 'rejected', why)
            for a in live:
                log[a] = (live[a], 'added', 'round %d' % rnd)
                added[a] = live[a]
            entries |= set(live)
            res.update(cres)   # listed code never reaches uncovered bytes: only new functions change
            print('round %d: %d candidates, %d added, %d rejected so far' % (rnd, len(cand), len(live), len(rejected)), flush=True)
            if live:
                continue
        # nothing new: check all new entries together against the old list
        bogus = {}
        live, rej, cres = settle(img, dict(added), base, base_cov, base, bogus)
        print('final check: %d new entries, %d dropped' % (len(added), len(rej)), flush=True)
        for a, why in rej.items():
            rejected[a] = why
            log[a] = (added[a], 'rejected', 'final check: ' + why)
            del added[a]
        entries = base | set(added)
        res = dict(base_res)
        res.update(cres)
        if not rej:
            break
    with open(new, 'w') as f:
        for a in sorted(entries):
            f.write('%08x\n' % a)
    if logp:
        with open(logp, 'w') as f:
            for a in sorted(log):
                s, v, why = log[a]
                f.write('%08x\t%s\t%s\t%s\n' % (a, ','.join(sorted(s)), v, why))
    bp = os.path.splitext(new)[0] + '-bad-old.txt'
    with open(bp, 'w') as f:
        f.write('# listed entry  new function that covers it (the listed entry starts inside one of its instructions)\n')
        for o in sorted(bogus):
            f.write('%08x %08x\n' % (o, bogus[o]))
    print('listed entries that start inside a real instruction: %d (%s)' % (len(bogus), bp))
    print('wrote %s: %d entries (%d old, %d new)' % (new, len(entries), len(base), len(added)))

if __name__ == '__main__':
    main()
