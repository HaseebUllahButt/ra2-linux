import sys; sys.path.insert(0,'/path/to/user/Projects/ra2/ra2-recomp/tools')
import lift as L
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
img=L.Image(L.EXE); md=Cs(CS_ARCH_X86, CS_MODE_32)
a=int(sys.argv[1],16); n=int(sys.argv[2])
for i in md.disasm(img.read(a, n*8), a):
    print('%08x %-8s %s' % (i.address, i.mnemonic, i.op_str)); n-=1
    if n==0: break
