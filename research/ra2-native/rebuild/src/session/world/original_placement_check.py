#!/usr/bin/env python3
"""Compare the staged clear-cell placement with the original base chain.

The original Unit, Foot, Techno, Object, cell-entry, footprint, link insertion,
and house count instructions run. Map records and non-gameplay services are
supplied. Visibility, map updates, locomotor services, and other effects are
not yet implemented by this fixture.
"""
import pathlib
import os
import subprocess
import sys
if len(sys.argv)!=3: raise SystemExit('usage: original_placement_check.py game.exe placement_case')
exe,native=sys.argv[1:]
probe=pathlib.Path(__file__).with_name('original_chain_probe.py')
for battle in (False,True):
  for level in (0,4):
    for bias in (0,1,255):
      for native_mark,original_mark in ((0,0),(1,1),(-1,0),(-1,1)):
        for native_flag,original_flag in ((0,0),(1,1),(-1,0),(-1,1)):
          args=[sys.executable,str(probe),exe,'--base',str(bias),str(level),str(original_mark)]+(['--battle'] if battle else [])+(['--flag200'] if original_flag else [])
          lines=subprocess.check_output(args,text=True).splitlines()
          original=[line[13:] for line in lines if line.startswith('PLACE_RESULT ')]
          actual=subprocess.check_output([native,str(bias),str(level),str(native_mark),str(native_flag)],text=True).splitlines()
          assert original==actual,[(x[:100],y[:100]) for x,y in zip(original,actual)]
          assert len(original)==2
print('384 base Unit blocked and successful placements match original checked fields')
for divisor in (1,50,2000):
    environment=dict(os.environ,RA2_SIGHT_DIVISOR=str(divisor))
    args=[sys.executable,str(probe),exe,'--base','0','4','0','--battle']
    lines=subprocess.check_output(args,text=True,env=environment).splitlines()
    original=[line[13:] for line in lines if line.startswith('PLACE_RESULT ')]
    actual=subprocess.check_output([native,'0','4','0','0'],text=True,env=environment).splitlines()
    assert len(original)==2 and original==actual
    assert int(original[1].split()[10])==((416//divisor)&255)*10%256
print('raised-cell placement matches three separate sight divisors, including low-byte wrap')
