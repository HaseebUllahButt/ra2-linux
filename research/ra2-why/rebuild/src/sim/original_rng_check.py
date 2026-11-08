#!/usr/bin/env python3
"""Optional evidence check. Requires Unicorn; never needed by the native core.
Run: python3 original_rng_check.py /path/to/user/ra2/game/gamemd.exe
Only the constructor and random-draw functions execute in an x86 emulator.
No operating system, game startup, Windows calls, or Wine is used.
"""
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP

image = open(sys.argv[1], 'rb').read()
expected = [
    (0, [0x49466cf3, 0x6a2b5556, 0x9089e025, 0x9cefdb3b, 0x06c7fb77]),
    (1, [0x78b76ed5, 0x275d74ae, 0xda63b931, 0xacebbad2, 0xe74254d1]),
    (0xffffffff, [0xf9b05e3c, 0xe8f24841, 0x66815a3f, 0x3b17004b, 0x40f2b4d8]),
]
for seed, golden in expected:
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    # This image has identical file offsets and RVAs for the needed sections.
    assert image[0x65c6d0 - 0x400000:0x65c6d3 - 0x400000] == b'\x83\xec\x14'
    cpu.mem_map(0x400000, 0x500000)
    cpu.mem_write(0x400000, image[:0x500000])
    cpu.mem_map(0x1000000, 0x20000)

    def call(address, argument=None):
        cpu.reg_write(UC_X86_REG_ECX, 0x1000000)
        cpu.reg_write(UC_X86_REG_ESP, 0x101f000)
        stack = struct.pack('<I', 0x101e000)
        if argument is not None:
            stack += struct.pack('<I', argument)
        cpu.mem_write(0x101f000, stack)
        cpu.emu_start(address, 0x101e000, count=100000)
        return cpu.reg_read(UC_X86_REG_EAX)

    call(0x65c6d0, seed)
    actual = [call(0x65c780) for _ in range(5)]
    assert actual == golden, (seed, actual)
print('15 original x86 random draws match native test vectors')
