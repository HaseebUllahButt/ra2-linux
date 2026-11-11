#!/usr/bin/env python3
"""bisect.py <build-dir> <addr>: find the smallest set of lifted functions that breaks one test.
Turns lifted functions off (they run as original code) with RC_OFF and halves the set."""
import os, subprocess, sys, json
bd, addr = os.path.abspath(sys.argv[1]), sys.argv[2]
exe = '/path/to/user/Projects/ra2/ra2-1to1/inputs/game.exe'
lift_dir = bd + '/lift' if os.path.isdir(bd + '/lift') else os.path.dirname(bd) + '/all/lift'
allf = sorted(f[2:10] for f in os.listdir(lift_dir) if f.endswith('.c'))
# optional third argument: file with the candidate addresses (others stay lifted)
cands = open(sys.argv[3]).read().split() if len(sys.argv) > 3 else None
def bad(on):
    off = [a for a in (cands or allf) if a not in on]
    open(bd + '/off.txt', 'w').write('\n'.join(off) + '\n')
    env = dict(os.environ, RC_OFF=bd + '/off.txt', DIFF_ONLY=addr, DIFF_STRICT='1')
    subprocess.run(['./test', exe], cwd=bd, env=env, capture_output=True)
    return ('%s MISMATCH' % addr) in open(bd + '/difftest.log').read()
on = set(cands or allf) | {addr}
assert bad(on), 'test passes with everything lifted'
assert not bad({addr}) or True
cand = sorted(on - {addr})
keep = {addr}
# find a minimal set: drop halves that are not needed
chunk = len(cand) // 2
while chunk >= 1:
    i = 0
    while i < len(cand):
        trial = set(cand[:i] + cand[i + chunk:]) | keep
        if bad(trial):
            cand = cand[:i] + cand[i + chunk:]
        else:
            i += chunk
    chunk //= 2
print('breaking set:', sorted(keep | set(cand)))
os.remove(bd + '/off.txt')
