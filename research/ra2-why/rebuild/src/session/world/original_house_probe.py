#!/usr/bin/env python3
"""Development probe for the original base House constructor."""
import struct
import sys
from unicorn import UcError,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
import original_route_check as r
m=r.m;cpu=m.cpu
if not m.base: raise SystemExit('base game.exe and --base are required')
r.setup((0,),15)
cpu.mem_map(0x1400000,0x80000);cpu.mem_map(0x1500000,0x1000000)
house,rules,house_type,scenario=0x1400000,0x1420000,0x1430000,0x1440000
cpu.mem_write(house,bytes([0xa5])*0x16000)
m.word(0x839848,rules);m.word(0xa3d290,scenario)
seed=int(sys.argv[3]) if len(sys.argv)>3 else 42
registry_size=int(sys.argv[4]) if len(sys.argv)>4 else 512
color=int(sys.argv[5]) if len(sys.argv)>5 else 0
m.word(0xa35d58,registry_size)
cpu.mem_write(house_type+0x24,b'Americans\0')
cpu.mem_write(0x1450000,b'Americans\0');m.word(house_type+0x60,0x1450000)
next_allocation=0x1500000
def allocate():
    global next_allocation
    size=m.argument();address=next_allocation;next_allocation+=(size+15)&~15
    assert next_allocation<0x2500000
    cpu.reg_write(UC_X86_REG_EAX,address)
m.bind(0x782017,0,allocate)
# COM reference accounting uses Windows interlocked functions. The probe does
# not run concurrent threads or expose those reference counts.
m.bind(0x492720,4,m.ret(1));m.bind(0x492740,4,m.ret(1))
cpu.reg_write(UC_X86_REG_ECX,scenario+0x218);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,seed));cpu.emu_start(0x638790,0x103e000,count=100000)
rng_calls=[]
house_draw=[]
def trace_rng(machine,address,size,user):
    if address in (0x638840,0x6388a0):
        esp=machine.reg_read(UC_X86_REG_ESP)
        rng_calls.append((hex(address),hex(m.read(esp)),m.signed(esp+4),m.signed(esp+8)))
    if address==0x4e25fb: house_draw.append(machine.reg_read(UC_X86_REG_EAX))
cpu.hook_add(UC_HOOK_CODE,trace_rng)
cpu.reg_write(UC_X86_REG_ECX,house);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,house_type))
try: cpu.emu_start(0x4e18c0,0x103e000,count=1000000)
except UcError:
    print('unresolved House constructor dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in m.recent], 'rng',rng_calls);raise
assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000
print('original House constructor returned; unit count',m.read(house+0x228),
      'type registry size',m.read(house+0x1a78))
print('rng calls',rng_calls,'indexes',m.read(scenario+0x21c),m.read(scenario+0x220))
assert len(house_draw)==1
cpu.reg_write(UC_X86_REG_ECX,house);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
cpu.mem_write(0x103f000,struct.pack('<4I',0x103e000,color,0,5000));cpu.emu_start(0x4e8300,0x103e000,count=10000)
cpu.reg_write(UC_X86_REG_ESP,0x103f000)
cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,color));cpu.emu_start(0x6715d0,0x103e000,count=10000)
palette=cpu.reg_read(UC_X86_REG_EAX)
state=bytes(cpu.mem_read(scenario+0x218,1012))
indexes=struct.unpack_from('<II',state,4);table=struct.unpack_from('<250I',state,12)
print('RESULT',house_draw[0],m.read(house+0x124),m.read(house+0x24c),m.read(house_type+0xc0),palette,
      m.read(house+0x228),m.read(house+0x1a78),*indexes,*table)
if '--aux' in sys.argv:
    cpu.mem_map(0x2500000,0x40000)
    for index,name in enumerate(('Neutral','Special')):
        aux_house=0x2500000+index*0x20000
        aux_type=0x1460000+index*0x1000
        aux_name=0x1450100+index*0x100
        cpu.mem_write(aux_house,bytes([0xa5])*0x16000)
        cpu.mem_write(aux_type+0x24,name.encode('ascii')+b'\0')
        cpu.mem_write(aux_name,name.encode('ascii')+b'\0');m.word(aux_type+0x60,aux_name)
        cpu.reg_write(UC_X86_REG_ECX,aux_house);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
        cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,aux_type))
        try: cpu.emu_start(0x4e18c0,0x103e000,count=1000000)
        except UcError:
            print('unresolved auxiliary House constructor dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in m.recent]);raise
if '--unit' in sys.argv:
    type_index=int(sys.argv[6]);excluded=int(sys.argv[7])
    cpu.mem_write(m.FOOT,bytes([0xa5])*0x1000)
    m.word(m.TYPE+0x8c,1000);m.word(m.TYPE+0xa0,1000)
    cpu.mem_write(m.TYPE+0x202,bytes([excluded]))
    locomotor_vtable,type_vtable=0x1460000,0x1461000
    m.word(m.LOCO+4,locomotor_vtable)
    for index,pop in ((4,4),(8,4),(12,8)):
        stub=0x1018000+index*4
        m.word(locomotor_vtable+index,stub);m.bind(stub,pop,m.ret(1))
    def create_locomotor():
        m.word(cpu.reg_read(UC_X86_REG_ECX),m.LOCO+4)
        cpu.reg_write(UC_X86_REG_EAX,0)
    m.bind(0x41b650,12,create_locomotor)
    m.word(m.TYPE,type_vtable);m.word(type_vtable+0x40,0x1018100)
    m.bind(0x1018100,0,m.ret(type_index))
    cpu.reg_write(UC_X86_REG_ECX,m.FOOT);cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<III',0x103e000,m.TYPE,house))
    try: cpu.emu_start(0x6fa400,0x103e000,count=1000000)
    except UcError:
        print('unresolved joint constructor dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in m.recent]);raise
    state=bytes(cpu.mem_read(scenario+0x218,1012))
    indexes=struct.unpack_from('<II',state,4);table=struct.unpack_from('<250I',state,12)
    low=struct.unpack('<H',cpu.mem_read(m.FOOT+0x320,2))[0]
    on_map=cpu.mem_read(m.FOOT+0x32d,1)[0]
    hull=struct.unpack('<H',cpu.mem_read(m.FOOT+0x2e0,2))[0]>>8
    turret=struct.unpack('<H',cpu.mem_read(m.FOOT+0x2f8,2))[0]>>8
    dynamic=m.read(house+0x5434)
    dynamic_count=m.read(dynamic+4*type_index) if dynamic else 0
    fixed_count=m.read(house+0x1278+4*type_index)
    if '--runtime-fields' in sys.argv:
        print('CONSTRUCTOR_FIELDS',*(cpu.mem_read(m.FOOT+offset,1)[0] for offset in (0x74,0x76,0x370)))
    print('UNIT_RESULT',low,m.read(m.FOOT+0x30),on_map,m.read(m.FOOT+0x6c),hull,turret,cpu.mem_read(m.FOOT+0x5a0,1)[0],
          m.read(house+0x228),dynamic_count,fixed_count,*indexes,*table)
