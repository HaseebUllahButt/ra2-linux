"""Read-only PE32 image loader for optional original x86 evidence checks."""
import pathlib
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32

def load_original(path):
    raw = pathlib.Path(path).read_bytes()
    if raw[:2] != b'MZ':
        raise ValueError('expected original PE image')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    if raw[pe:pe+4] != b'PE\0\0':
        raise ValueError('invalid PE header')
    count = struct.unpack_from('<H', raw, pe+6)[0]
    optional_size = struct.unpack_from('<H', raw, pe+20)[0]
    optional = pe+24
    if struct.unpack_from('<H', raw, optional)[0] != 0x10b:
        raise ValueError('expected PE32')
    base = struct.unpack_from('<I', raw, optional+28)[0]
    size = struct.unpack_from('<I', raw, optional+56)[0]
    if base != 0x400000:
        raise ValueError('unexpected original preferred base')
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    cpu.mem_map(base, (size+4095) & ~4095)
    header_size = struct.unpack_from('<I', raw, optional+60)[0]
    cpu.mem_write(base, raw[:header_size])
    for i in range(count):
        section = optional+optional_size+i*40
        rva, raw_size, offset = struct.unpack_from('<III', raw, section+12)
        if raw_size:
            cpu.mem_write(base+rva,raw[offset:offset+raw_size])
    return cpu
