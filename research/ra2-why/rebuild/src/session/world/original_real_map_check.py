#!/usr/bin/env python3
"""Compare MP02T2 waypoint inputs with original x86 placement.

The map geometry, waypoint, terrain, Unit type and registry come from the base
assets. Runtime cell state, the Unit type index, and the blocked water cell are
fixture inputs. The original visibility, map update, and locomotor services
are adapted.
"""
import os
import pathlib
import subprocess
import sys

if len(sys.argv) not in (3,5):
    raise SystemExit('usage: original_real_map_check.py game.exe placement_case [real_map_case game_dir]')
exe, native=sys.argv[1:3]
probe=pathlib.Path(__file__).with_name('original_chain_probe.py')
environment=dict(os.environ, RA2_WORLD_MAP_FIXTURE='1')
if len(sys.argv)==5:
    divisor=subprocess.check_output([sys.argv[3],sys.argv[4],'--sight-info'],text=True).strip()
    assert divisor=='2000'
    environment['RA2_SIGHT_DIVISOR']=divisor
lines=subprocess.check_output([sys.executable,str(probe),exe,'--base','0','4','0','--battle'],
                              text=True,env=environment).splitlines()
original=[line[13:] for line in lines if line.startswith('PLACE_RESULT ')]
actual=subprocess.check_output([native,'0','4','0','0'],text=True,env=environment).splitlines()
assert len(original)==2 and original==actual
if len(sys.argv)==5:
    assert original[1].split()[9:11]==['416','0']
suffix=' with installed sight divisor 2000' if len(sys.argv)==5 else ' with synthetic sight divisor 104'
print('MP02T2 waypoint and synthetic blocked cell match adapted original x86 placement'+suffix)
