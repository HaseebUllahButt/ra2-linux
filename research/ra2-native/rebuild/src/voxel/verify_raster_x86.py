#!/usr/bin/env python3
"""Compare HTNK raster hashes with the original x86 routines via Unicorn.

Pass the path of the voxel test executable. It supplies original asset bytes.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
from verify_coverage_x86 import BASE, STACK, STOP, setup

if len(sys.argv) != 2:
    raise SystemExit('usage: verify_raster_x86.py path/to/voxel_test')

def asset(name):
    return subprocess.check_output([sys.argv[1], '--asset', name])

vxl, vpl = asset('htnk.vxl'), asset('voxels.vpl')
header = 32 + struct.unpack_from('<I', vxl, 16)[0] * 770
body = header + 28
size = struct.unpack_from('<I', vxl, 28)[0]
tail = body + size
width, height, depth, normals = vxl[tail + 88:tail + 92]
starts, ends, spans = struct.unpack_from('<3I', vxl, tail)
bounds = struct.unpack_from('<6f', vxl, tail + 64)
mn, mx = bounds[:3], bounds[3:]


def render(direction, lit):
    machine, word, real = setup()
    machine.mem_write(BASE + 0x10000, vxl[body:tail])
    for offset, value in ((0x200, starts), (0x204, ends), (0x208, spans)):
        word(BASE + offset, BASE + 0x10000 + value)
    machine.mem_write(BASE + 0x2a0, bytes((width, height, depth, normals)))
    corners = [(mx[0], mx[1], mn[2]), (mx[0], mn[1], mn[2]),
               (mn[0], mn[1], mn[2]), (mn[0], mx[1], mn[2]),
               (mx[0], mx[1], mx[2]), (mx[0], mn[1], mx[2]),
               (mn[0], mn[1], mx[2]), (mn[0], mx[1], mx[2])]
    for i, corner in enumerate(corners):
        for axis, value in enumerate(corner):
            real(BASE + 0x240 + i * 12 + axis * 4, value)
    matrix = [1, -1, 0, 0, .5, .5, 1, 0,
              -.2 if direction & 1 else .2, -.3 if direction & 2 else .3,
              -1 if direction & 4 else 1, 0]
    for i, value in enumerate(matrix):
        real(BASE + 0x700 + 4 * i, value)
    for address, value in ((0xb2d5e0, 10000), (0xb2d5e4, 10000),
                           (0xb2d5e8, 10000), (0xb2d948, -10000),
                           (0xb2d94c, -10000), (0xb2d950, -10000)):
        real(address, value)
    for i, value in enumerate((STOP, 0, BASE + 0x700)):
        word(STACK + 4 * i, value)
    machine.reg_write(UC_X86_REG_ECX, BASE)
    machine.reg_write(UC_X86_REG_EDX, 0)
    machine.reg_write(UC_X86_REG_ESP, STACK)
    machine.emu_start(0x7540f0, STOP, count=100000)
    entry, center = 0xb2d958, BASE + 0x900
    for axis in range(3):
        low = struct.unpack('<f', machine.mem_read(entry + 16 + axis * 4, 4))[0]
        high = struct.unpack('<f', machine.mem_read(entry + 28 + axis * 4, 4))[0]
        real(center + 4 * axis, (low + high) / 2)
    if lit:
        word(0xb43184, 1)
        machine.mem_write(0xb45990, bytes(i % 32 for i in range(256)))
        machine.mem_write(0xb41178, vpl[784:])
    for i, value in enumerate((STOP, entry, center)):
        word(STACK + 4 * i, value)
    machine.reg_write(UC_X86_REG_ECX, BASE)
    machine.reg_write(UC_X86_REG_ESP, STACK)
    machine.emu_start(0x756590, STOP, count=10000000)
    pixels = machine.mem_read(0xb2ff78, 65536)
    if not lit and direction == 0 and width == 46:
        output = BASE + 0xa00
        word(STACK, STOP)
        machine.reg_write(UC_X86_REG_ECX, output)
        machine.reg_write(UC_X86_REG_ESP, STACK)
        machine.emu_start(0x754510, STOP, count=10000000)
        assert struct.unpack('<6i', machine.mem_read(output, 24)) == (-40,-36,85,99,86,58)
    value = 14695981039346656037
    for pixel in pixels:
        value = ((value ^ pixel) * 1099511628211) & 0xffffffffffffffff
    return value

expected = [
    [0xe94e713a7da26f85, 0x31d5532f1ebb6683, 0x67288c0a6e6dc2a7, 0x5a02e9ef37c11204,
     0x7accc6d5c7971c81, 0x2b42f044298bacd5, 0xe49771ab954cfb16, 0xf8681956dd883719],
    [0x22033d5fb411b1b6, 0xfacfd818113b9c3b, 0x15dd8d3e13071ae8, 0x38f42422ba0e9945,
     0x27dd6dc261980fd8, 0x54f887d1ece4e457, 0xab31afb7bec384e6, 0xdf061d334a8fa1fc],
]
for lit in (0, 1):
    for direction in range(8):
        assert render(direction, lit) == expected[lit][direction]
vxl = asset('mtnktur.vxl')
header = 32 + struct.unpack_from('<I', vxl, 16)[0] * 770
body = header + 28
size = struct.unpack_from('<I', vxl, 28)[0]
tail = body + size
width, height, depth, normals = vxl[tail + 88:tail + 92]
starts, ends, spans = struct.unpack_from('<3I', vxl, tail)
bounds = struct.unpack_from('<6f', vxl, tail + 64)
mn, mx = bounds[:3], bounds[3:]
assert render(0, True) == 0xc923cfb562c50019
assert render(7, True) == 0x058b2abb9bc4edf5
print('Original x86: 16 HTNK and two MTNKTUR raster hashes passed')
