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
if len(sys.argv)==5:
    zero_z=dict(environment,RA2_INITIAL_Z='0',RA2_INITIAL_FACING='0',RA2_RUNTIME_FIELDS='1')
    lines=subprocess.check_output([sys.executable,str(probe),exe,'--base','0','4','0','--battle'],
                                  text=True,env=zero_z).splitlines()
    original_zero=[line[13:] for line in lines if line.startswith('PLACE_RESULT ')]
    native_lines=subprocess.check_output([native,'0','4','0','0'],text=True,env=zero_z).splitlines()
    original_fields=[line for line in lines if line.startswith('RUNTIME_FIELDS ')]
    native_fields=[line for line in native_lines if line.startswith('RUNTIME_FIELDS ')]
    assert original_fields==native_fields==['RUNTIME_FIELDS 0 1 0','RUNTIME_FIELDS 1 0 0']
    native_zero=[line for line in native_lines if not line.startswith('RUNTIME_FIELDS ')]
    assert len(original_zero)==2 and original_zero==native_zero
    assert original_zero[1].split()[7:11]==['0','0','0','0']
    print('MP02T2 supplied runtime fixture preserves Battle start Z zero during original placement')

suffix=' with installed sight divisor 2000' if len(sys.argv)==5 else ' with synthetic sight divisor 104'
print('MP02T2 waypoint and synthetic blocked cell match adapted original x86 placement'+suffix)
