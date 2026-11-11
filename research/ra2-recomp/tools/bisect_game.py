#!/usr/bin/env python3
"""bisect_game.py: find the lifted function(s) that break a game run.
usage: bisect_game.py <out-dir> <steps> <check>   check: 'skirmish' (in-game sidebar shows, no divide fault)
Uses build/win/funcs.txt (all lifted functions). Each test runs tools/drive.sh with RC_ON=<list>.
Keeps a 'base' set that is always on; narrows the 'suspect' set by halves (handles pairs that
only fail together). Writes <out-dir>/bisect.log and prints the culprit."""
import os, subprocess, sys, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out, steps, check = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
allf = [l.split()[0] for l in open(ROOT + '/build/win/funcs.txt')]
log = open(out + '/bisect.log', 'a')
def say(s):
    print(s, flush=True); log.write(s + '\n'); log.flush()
n = 0
def test(on):
    global n
    n += 1
    d = '%s/t%03d' % (out, n)
    os.makedirs(d, exist_ok=True)
    open(d + '/on.txt', 'w').write('\n'.join(on) + '\n')
    env = dict(os.environ, RC_ON=d + '/on.txt')
    subprocess.run([ROOT + '/tools/drive.sh', d, steps], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    lg = open(d + '/rc.log', errors='replace').read() if os.path.exists(d + '/rc.log') else ''
    bad = 'c0000094' in lg
    if not bad and check == 'skirmish':
        r = subprocess.run(['compare', '-metric', 'AE', '-fuzz', '15%', '-crop', '150x150+865+20',
                            ROOT + '/build/menu/ingame-ref.png', d + '/g1.png', 'null:'], capture_output=True, text=True)
        try: diff = float(r.stderr.split()[0])
        except Exception: diff = 1e9
        bad = diff > 4000
    say('test %d: %d on -> %s' % (n, len(on), 'FAIL' if bad else 'pass'))
    return not bad
base, sus = [], list(allf)
if len(sys.argv) > 4:                   # resume: base and suspect files
    base = open(sys.argv[4]).read().split(); sus = open(sys.argv[5]).read().split()
t0 = time.time()
while len(sus) > 1:
    lo, hi = sus[:len(sus) // 2], sus[len(sus) // 2:]
    if not test(base + lo): sus = lo
    elif not test(base + hi): sus = hi
    else:
        say('both halves pass alone: keep low half on, search high half')
        base = base + lo; sus = hi
    open(out + '/base.txt', 'w').write('\n'.join(base)); open(out + '/sus.txt', 'w').write('\n'.join(sus))
    say('suspects %d (%s .. %s), base %d, %.0f min' % (len(sus), sus[0], sus[-1], len(base), (time.time() - t0) / 60))
say('CULPRIT %s (base %d functions)' % (sus[0], len(base)))
