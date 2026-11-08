#!/usr/bin/env python3
"""Check original special-house creation order with constructors supplied."""
import pathlib
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EIP,UC_X86_REG_ESP
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original
if len(sys.argv)!=3: raise SystemExit('usage: original_auxiliary_check.py game.exe auxiliary_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
entry,stop,alloc,resolve,construct,color,finish,types_global,color_field=((0x65ff35,0x65ffdd,0x782017,0x4f94d0,0x4e18c0,0x663d90,0x4f5400,0xa35d0c,0x15f40) if base else
    (0x6882d1,0x688378,0x7c8e17,0x5117d0,0x4f54a0,0x68cab0,0x50b840,0xa83c9c,0x16054))
cpu=load_original(exe);cpu.mem_map(0x1000000,0x60000)
objects=[0x1000000,0x1020000];types=0x1044000;type_values=[0x1045000,0x1046000]
stack=0x1050000
def put(address,*values): cpu.mem_write(address,struct.pack('<'+'I'*len(values),*(x&0xffffffff for x in values)))
def get(address): return struct.unpack('<I',cpu.mem_read(address,4))[0]
def string(address): return bytes(cpu.mem_read(address,32)).split(b'\0',1)[0].decode('ascii')
put(types_global,types);put(types,*type_values);put(stack,0,0,0,0)
events=[];calls=[]
def hook(machine,address,size,user):
    if address not in (alloc,resolve,construct,color,finish): return
    esp=machine.reg_read(UC_X86_REG_ESP);ret=get(esp);pop=0;value=0
    ecx=machine.reg_read(UC_X86_REG_ECX)
    if address==alloc:
        value=objects[len(calls)];calls.append(value)
    elif address==resolve:
        name=string(ecx);value={'Neutral':0,'Special':1}[name];events.append([name,None,None])
    elif address==construct:
        assert ecx==objects[len(events)-1] and get(esp+4)==type_values[len(events)-1]
        value=ecx;pop=4
    elif address==color:
        events[-1][1:]=[string(ecx),machine.reg_read(UC_X86_REG_EDX)];value=41
    else:
        assert ecx==objects[len(events)-1] and get(ecx+color_field)==41
    machine.reg_write(UC_X86_REG_EAX,value)
    machine.reg_write(UC_X86_REG_ESP,esp+4+pop);machine.reg_write(UC_X86_REG_EIP,ret)
cpu.hook_add(UC_HOOK_CODE,hook)
cpu.reg_write(UC_X86_REG_ESP,stack);cpu.emu_start(entry,stop,count=10000)
expected=[line.split() for line in subprocess.check_output([native],text=True).splitlines()]
assert events==[[name,scheme,int(arg)] for name,scheme,arg in expected],events
assert len(calls)==2
print('2 auxiliary houses match original x86:',pathlib.Path(exe).name)
