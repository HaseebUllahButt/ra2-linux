#!/usr/bin/env python3
"""Compare base House constructor, field copy, and complete RNG state.

The oracle executes the original constructor and field assignments. It
supplies memory allocation and COM reference counting.
"""
import pathlib
import subprocess
import sys
if len(sys.argv)!=3: raise SystemExit('usage: original_house_check.py game.exe house_case')
exe,native=sys.argv[1:]
probe=pathlib.Path(__file__).with_name('original_house_probe.py')
cases=[(seed,size,color) for seed in (0,1,42,4294967295) for size in (0,1,512) for color in (0,7)]
for seed,size,color in cases:
    args=list(map(str,(seed,size,color)))
    lines=subprocess.check_output([sys.executable,str(probe),exe,'--base',*args],text=True).splitlines()
    original=next(line[7:] for line in lines if line.startswith('RESULT '))
    actual=subprocess.check_output([native,*args],text=True).strip()
    assert original==actual,((seed,size,color),original[:120],actual[:120])
print(f'{len(cases)} base House constructors match original random state and fields')
