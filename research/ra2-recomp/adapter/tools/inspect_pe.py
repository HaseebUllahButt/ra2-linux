#!/usr/bin/env python3
"""Read PE32 imports and IAT call sites without third-party packages."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def inspect(path):
    data = Path(path).read_bytes()
    u16 = lambda p: struct.unpack_from('<H', data, p)[0]
    u32 = lambda p: struct.unpack_from('<I', data, p)[0]
    pe = u32(0x3c)
    if data[pe:pe+4] != b'PE\0\0' or u16(pe+24) != 0x10b:
        raise ValueError('Expected a PE32 image')
    base = u32(pe+52)
    sections = []
    start = pe+24+u16(pe+20)
    for n in range(u16(pe+6)):
        p = start+40*n
        sections.append((data[p:p+8].rstrip(b'\0').decode(), *struct.unpack_from('<IIII', data, p+8)))
    def offset(rva):
        for _, vs, va, size, raw in sections:
            if va <= rva < va+size:
                return raw+rva-va
        raise ValueError(f'RVA outside file: {rva:x}')
    def string(rva):
        p = offset(rva)
        return data[p:data.index(b'\0', p)].decode('ascii')
    imports = []
    p = offset(u32(pe+24+104))
    while u32(p+12):
        lookup, _, _, name, iat = struct.unpack_from('<IIIII', data, p)
        dll = string(name)
        t = offset(lookup or iat)
        n = 0
        while u32(t+4*n):
            value = u32(t+4*n)
            symbol = f'#{value & 0xffff}' if value & 0x80000000 else string(value+2)
            address = base+iat+4*n
            sites = []
            for sname, _, va, size, raw in sections:
                if sname != '.text':
                    continue
                code = data[raw:raw+size]
                needle = struct.pack('<I', address)
                for opcode in (b'\xff\x15', b'\xff\x25'):
                    pos = code.find(opcode+needle)
                    while pos >= 0:
                        sites.append(f'0x{base+va+pos:08x}')
                        pos = code.find(opcode+needle, pos+1)
            imports.append(dict(dll=dll, symbol=symbol, iat=f'0x{address:08x}', direct_sites=sorted(sites)))
            n += 1
        p += 20
    return dict(image=str(path), sha256=hashlib.sha256(data).hexdigest(), imports=imports,
                note='Direct byte-pattern call sites only; COM and dynamically loaded methods need separate tracing.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('image')
    args = parser.parse_args()
    print(json.dumps(inspect(args.image), indent=2))
