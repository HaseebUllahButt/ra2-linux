#!/usr/bin/env python3
"""Run base House then Unit constructors with one shared original RNG."""
import pathlib
import subprocess
import sys
if len(sys.argv)!=3: raise SystemExit('usage: original_joint_check.py game.exe constructor_case')
exe,native=sys.argv[1:]
probe=pathlib.Path(__file__).with_name('original_house_probe.py')
cases=[(seed,index,excluded,size,aux) for seed in (0,42,4294967295) for index in (0,3) for excluded in (0,1) for size in (64,512) for aux in (False,True)]
for seed,index,excluded,size,aux in cases:
    args=list(map(str,(seed,index,excluded,size)))+(['--aux'] if aux else [])
    original_args=[str(seed),str(size),'0',str(index),str(excluded),'--unit']
    if aux: original_args.append('--aux')
    lines=subprocess.check_output([sys.executable,str(probe),exe,'--base',*original_args],text=True).splitlines()
    original=next(line[12:] for line in lines if line.startswith('UNIT_RESULT '))
    actual=subprocess.check_output([native,*args],text=True).strip()
    assert original==actual,((seed,index,excluded,size,aux),original[:120],actual[:120])
print(f'{len(cases)} base House and Unit sequences match original random state and fields')
