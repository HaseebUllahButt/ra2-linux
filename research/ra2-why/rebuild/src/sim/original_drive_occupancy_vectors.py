#!/usr/bin/env python3
"""Check 8 base straight-drive occupancy traces with explicit world adapters."""
import sys,runpy,contextlib,io,struct,pathlib
root=pathlib.Path(__file__).resolve().parent
sys.path.insert(0,str(root))
if len(sys.argv) not in (2,3): raise SystemExit('usage: original_drive_occupancy_vectors.py /path/to/game.exe [--vectors]')
exe=sys.argv[1]; vectors=len(sys.argv)==3 and sys.argv[2]=='--vectors'
sys.argv=['original_chain_probe.py',exe,'--base','0','0','0']
with contextlib.redirect_stdout(io.StringIO()): f=runpy.run_path(str(root.parent/'session/world/original_chain_probe.py'))
r=f['r'];m=f['m'];cpu=m.cpu
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP
slots=(0x11c,0x74,0x80,0x84,0x100,0xbc,0xe8,0x12c); saved={s:m.read(m.VT+s) for s in slots};tb=bytes(cpu.mem_read(m.TYPE,0x1000))
dirs=((0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1))
def current():
 x=m.signed(m.FOOT+0x88)//256;y=m.signed(m.FOOT+0x8c)//256;m.word(0x1009000,x|(y<<16));cpu.reg_write(UC_X86_REG_EAX,0x1009000)
h=2166136261;count=0
for direction,(dx,dy) in enumerate(dirs):
 for speed in (1,7,15,30,255):
  case_hash=2166136261;case_frames=0
  r.setup((direction,direction),speed);cpu.mem_write(m.TYPE,tb);cpu.mem_write(m.FOOT+0x5a0,b'\x01')
  for slot,v in saved.items():m.word(m.VT+slot,v)
  m.bind(0x1015000,4,current);m.word(m.VT+0x1a0,0x1015000)
  m.word(m.FOOT+0x564,m.LOCO+4);m.word(m.VT+0x19c,0x4ca160);m.word(m.VT+0x1b4,0x5d4860);m.word(m.VT+0xec,0x7079e0);m.word(0xad057c,416)
  m.word(m.FOOT+0x30,0);cpu.mem_write(m.FOOT+0x74,b'\x01\x01\x00');m.word(m.TYPE+0xaa0,0);m.word(m.FOOT+0x370,0)
  for x in range(50,59):
   for y in range(65,74):
    c=m.cell_for(x,y);m.word(c+0x140,0);m.word(c+0x124,0);m.word(c+0x128,0);m.word(c+0xe4,0);m.word(c+0xe8,0)
  coords=[(54+i*dx,69+i*dy) for i in range(3)];source=m.cell_for(*coords[0]);m.word(source+0xe4,m.FOOT);m.word(source+0x124,32)
  for frame in range(1,10000):
   m.word(0xa40d2c,frame);cpu.reg_write(UC_X86_REG_ESP,0x103f000);cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,m.LOCO+4));cpu.emu_start(r.wrapper,0x103e000,count=200000);assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000
   state=[direction,speed,frame,m.signed(m.FOOT+0x88),m.signed(m.FOOT+0x8c),cpu.mem_read(m.FOOT+0x5a0,1)[0]]
   for p in coords:
    c=m.cell_for(*p);head=m.read(c+0xe4);assert head in (0,m.FOOT);state.extend((int(head==m.FOOT),m.read(c+0x124)))
   for n in state:
    h=((h^(n&0xffffffff))*16777619)&0xffffffff
    case_hash=((case_hash^(n&0xffffffff))*16777619)&0xffffffff
   count+=1;case_frames+=1
   if m.signed(m.LOCO+0x58)==-1 and m.signed(m.FOOT+0x4d0)==-1:break
  else:raise AssertionError('loop')
  if vectors: print('{%d,%d,%d,0x%08xu},'%(direction,speed,case_frames,case_hash))
assert count==3932 and h==0x413d4d59,(count,hex(h))
if not vectors: print(count,hex(h),'base original frames across eight directions and five speeds')
