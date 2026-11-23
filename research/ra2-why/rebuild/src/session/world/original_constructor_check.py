#!/usr/bin/env python3
"""Compare one base Unit constructor and the complete shared RNG state.

The oracle runs the original Unit and parent constructors and house registry
increments. It supplies an allocator and the locomotor interface.
"""
import pathlib
import subprocess
import sys
if len(sys.argv)!=3: raise SystemExit('usage: original_constructor_check.py game.exe constructor_case')
exe,native=sys.argv[1:]
probe=pathlib.Path(__file__).with_name('original_constructor_probe.py')
cases=[(seed,index,excluded) for seed in (0,1,42,4294967295) for index in (0,3,511) for excluded in (0,1)]
for seed,index,excluded in cases:
    args=list(map(str,(seed,index,excluded)))
    lines=subprocess.check_output([sys.executable,str(probe),exe,'--base',*args],text=True).splitlines()
    original=next(line[7:] for line in lines if line.startswith('RESULT '))
    actual=subprocess.check_output([native,*args],text=True).strip()
    assert original==actual,((seed,index,excluded),original[:120],actual[:120])
print(f'{len(cases)} base Unit constructors match original random state and house counts')
