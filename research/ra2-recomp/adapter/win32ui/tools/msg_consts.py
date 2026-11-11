#!/usr/bin/env python3
"""msg_consts.py: guess the message number at each SendMessageA / PostMessageA / SendDlgItemMessageA call.
usage: msg_consts.py import-sites.txt > msgs.txt
Walks back from the call over the pushes. The message is push #2 (Send/Post) or #3 (SendDlgItem).
Only immediate pushes are named; others print '?'. Static guess: the recorder trace is the real list."""
import sys, os, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import lift as L
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
img = L.Image(L.EXE); md = Cs(CS_ARCH_X86, CS_MODE_32)
WANT = {'SendMessageA': 2, 'PostMessageA': 2, 'SendDlgItemMessageA': 3, 'CallWindowProcA': 3, 'GetWindowLongA': 2, 'SetWindowLongA': 2}
cnt = collections.Counter()
for line in open(sys.argv[1]):
    f = line.split()
    if len(f) != 5 or f[2] not in ('call', 'call-reg', 'call-thunk') or f[4] not in WANT: continue
    site, func = int(f[0], 16), int(f[1], 16)
    ins = list(md.disasm(img.read(func, site - func + 8), func))
    idx = [i for i, x in enumerate(ins) if x.address == site]
    if not idx: cnt[(f[4], '?nodis')] += 1; continue
    k = idx[0]; pushes = []
    while k > 0 and len(pushes) < 4:
        k -= 1; x = ins[k]
        if x.mnemonic == 'push': pushes.append(x.op_str)
        elif x.mnemonic in ('call', 'ret', 'jmp') or x.mnemonic.startswith('j'): break
    want = WANT[f[4]]
    v = pushes[want - 1] if len(pushes) >= want else '?'
    try: v = '0x%x' % int(v, 0)
    except ValueError: v = '?'
    cnt[(f[4], v)] += 1
    if os.environ.get("SITES") and f[4] in os.environ["SITES"].split(","): print("site", f[4], v, hex(site), hex(func))
for (fn, v), n in sorted(cnt.items()): print(fn, v, n)
