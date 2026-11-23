#!/usr/bin/env python3
"""Compare bounded promotion arithmetic with base game.exe (no match runs).
Usage: python verify_kill_promotion_x86.py game/game.exe /tmp/ra2-kill-promotion-test
The caller builds one native executable outside rebuild/build.
"""
import math
import random
import struct
import subprocess
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_FPCW

cpu = Uc(UC_ARCH_X86, UC_MODE_32)
cpu.mem_map(0x400000, 0x800000)
cpu.mem_write(0x400000, Path(sys.argv[1]).read_bytes()[:0x431000])
cpu.mem_map(0x1000000, 0x200000)
stack, stop, house, country, kind_type, vtable, rules, rank_ptr = (
    0x1010000 + n for n in (0, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x8000))
def w(addr, value):
    cpu.mem_write(addr, struct.pack('<I', value & 0xffffffff))
def d(addr, value):
    cpu.mem_write(addr, struct.pack('<d', value))
def r(addr):
    return struct.unpack('<I', cpu.mem_read(addr, 4))[0]
def ret(value):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    cpu.reg_write(UC_X86_REG_EAX, value)
    cpu.reg_write(UC_X86_REG_EIP, r(sp))
    cpu.reg_write(UC_X86_REG_ESP, sp + 4)
kind, cost = 0x10, 200
def hook(_cpu, addr, _size, _data):
    ret(kind if addr == stop + 16 else cost)
for addr in (stop + 16, stop + 32):
    cpu.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)
w(kind_type, vtable)
w(vtable + 0x2c, stop + 16)
w(vtable + 0xac, stop + 32)
w(house + 0x34, country)
w(0x839848, rules)
# Confirm the executable's conversion helper requests the same control word.
assert r(0x7d8964) == 0xe7f, hex(r(0x7d8964))
def call(address, this, args):
    w(stack, stop)
    for i, arg in enumerate(args):
        w(stack + 4 + 4*i, arg)
    cpu.reg_write(UC_X86_REG_ESP, stack)
    cpu.reg_write(UC_X86_REG_ECX, this)
    cpu.reg_write(UC_X86_REG_FPCW, 0xe7f)
    cpu.emu_start(address, stop, count=10000)
    return cpu.reg_read(UC_X86_REG_EAX)
def original_price(c, factor, category):
    global kind, cost
    kind, cost = category, c
    cpu.mem_write(country + (0x114 if kind == 0x10 else 0x118), struct.pack('<f', factor))
    return call(0x6dad30, kind_type, [house])
def original_gain(value, kp, vp, vr, ratio, cap):
    d(rank_ptr, value)
    d(rules + 0x588, ratio)
    d(rules + 0x5b8, cap)
    credit = vp * (3 if vr >= 2 else 2 if vr >= 1 else 1)
    call(0x713360, rank_ptr, [kp, credit])
    return struct.unpack('<d', cpu.mem_read(rank_ptr, 8))[0]
rng = random.Random(0x713360)
cases = []
for i in range(1200):
    c = rng.randrange(1, 50000)
    factor = struct.unpack('<f', struct.pack('<f', rng.choice([0.5, 0.8, 1, 1.1, 1.5, 2, 3.7])))[0]
    kp, vp = rng.randrange(1, 50000), rng.randrange(1, 50000)
    vr = rng.choice([0., math.nextafter(1., 0.), 1., math.nextafter(2., 0.), 2., 3.])
    value = rng.choice([0., math.nextafter(1., 0.), 1., math.nextafter(2., 0.), 2., rng.random()*2])
    ratio, cap = rng.choice([0.7, 1., 3., 7.]), rng.choice([1., 2., 4.])
    cases.append((c, factor, value, kp, vp, vr, ratio, cap))
value = 0.
for i in range(14):
    case = (200, 1., value, 200, 100, 0., 3., 2.)
    cases.append(case)
    value = original_gain(*case[2:])
lines = subprocess.check_output([sys.argv[2], '--arithmetic'], text=True,
    input=''.join(' '.join(map(str, c))+'\n' for c in cases)).splitlines()
assert len(lines) == len(cases)
for i, (case, line) in enumerate(zip(cases, lines)):
    c, factor, value, kp, vp, vr, ratio, cap = case
    actual_price, actual_gain = line.split()
    for category in (0x10, 0x28):
        expected = original_price(c, factor, category)
        assert int(actual_price) == expected, (i, case, actual_price, expected)
    expected = original_gain(value, kp, vp, vr, ratio, cap)
    assert struct.pack('<d', float(actual_gain)) == struct.pack('<d', expected), (i, case, actual_gain, expected)
print(f'PASS: {len(cases)} original 00713360 gains and {2*len(cases)} original 006DAD30 country prices; exact double bits')
