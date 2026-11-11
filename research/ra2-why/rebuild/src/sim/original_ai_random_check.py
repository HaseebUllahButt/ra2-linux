#!/usr/bin/env python3
"""Run base 1.006 team random call sites without starting a match.

006BEDA1 and 006BEF92 use [00A3D290]+218, not static Random 00839218.
004E464E calls the picker after the House +5684 timer expires.
00515956 seeds a temporary ring, then copies 0xFD words to Scenario +218.
The static ring is separately copied at 00515982; it is not the team owner.
These bounded slices prove ownership, range results, rejection and ring writes.
Original skirmish production order follows at 004E4DCF..004E4DE4:
buildings, vehicles, infantry, aircraft. Enemy selection 004E8990 runs at
004E4C02, between the picker and production. This check does not cover that draw.
They do not run eligibility checks, team constructors or the whole House AI.
"""
import random
import struct
import sys
from original_image import load_original
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_EIP

cpu = load_original(sys.argv[1])
cpu.mem_map(0x1000000, 0x20000)
scenario, house, stack = 0x1000000, 0x1003000, 0x101f000
ring = scenario + 0x218

def word(at, value):
    cpu.mem_write(at, struct.pack('<I', value & 0xffffffff))

def state(table, first, second):
    return struct.pack('<III250I', 0, first, second, *table)

# Pin the original ownership and House picker instructions.
assert bytes(cpu.mem_read(0x4e4648, 11)) == bytes.fromhex('8bd68d4c2440e8fda61d00')
assert bytes(cpu.mem_read(0x51595b, 0x17)) == bytes.fromhex(
    '8b0d90d2a3008d74241c8db918020000b9fd000000f3a5')
word(0xa3d290, scenario)
static_ring = bytes(cpu.mem_read(0x839218, 1012))
seen = []
def observe(cpu, address, size, user):
    seen.append(cpu.reg_read(UC_X86_REG_ECX))
cpu.hook_add(UC_HOOK_CODE, observe, begin=0x6388a0, end=0x6388a0)

def reference(table, first, second, low, high):
    if high < low:
        low, high = high, low
    if low == high:
        return low, first, second
    span = high - low
    mask = (1 << span.bit_length()) - 1
    while True:
        table[first] ^= table[second]
        value = table[first] & mask
        first, second = (first + 1) % 250, (second + 1) % 250
        if value <= span:
            return low + value, first, second

checks = 0
# House timer: no picker before expiry; expiry reaches 004E464E.
word(house + 0x34, scenario + 0x8000)
word(house + 0x5684, 0)
word(house + 0x568c, 10)
word(0xa3d298, 1)
for frame, target in ((9, 0x4e46ec), (10, 0x4e464e)):
    word(0xa40d2c, frame)
    cpu.reg_write(UC_X86_REG_ESI, house)
    cpu.reg_write(UC_X86_REG_ESP, stack)
    cpu.emu_start(0x4e45e3, target, count=1000)
    assert cpu.reg_read(UC_X86_REG_EIP) == target
    checks += 1
rng = random.Random(1006)
for initial in (0, 148, 249):
    for total in (1, 2, 50, 100, 5000, 10000):
        table = [rng.getrandbits(32) for _ in range(250)]
        first, second = initial, (initial + 103) % 250
        cpu.mem_write(ring, state(table, first, second))
        for start, stop, low, high in ((0x6bed92, 0x6beda6, 1, 100),
                                      (0x6bef83, 0x6bef97, 1, total)):
            cpu.reg_write(UC_X86_REG_ESP, stack)
            cpu.reg_write(UC_X86_REG_EAX, high)
            cpu.emu_start(start, stop, count=10000)
            value, first, second = reference(table, first, second, low, high)
            assert cpu.reg_read(UC_X86_REG_EAX) == value
            assert bytes(cpu.mem_read(ring, 1012)) == state(table, first, second)
            assert seen[-1] == ring
            checks += 1
# Match the native synthetic test: reject 127, accept 99, then pick team two.
table = [0] * 250
table[0], table[1], table[2] = 127, 99, 1
cpu.mem_write(ring, state(table, 0, 103))
for start, stop, expected in ((0x6bed92, 0x6beda6, 100), (0x6bef83, 0x6bef97, 2)):
    cpu.reg_write(UC_X86_REG_ESP, stack)
    cpu.reg_write(UC_X86_REG_EAX, 2)
    cpu.emu_start(start, stop, count=10000)
    assert cpu.reg_read(UC_X86_REG_EAX) == expected
    checks += 1
assert struct.unpack('<II', cpu.mem_read(ring + 4, 8)) == (3, 106)
# The ratio draw precedes both rejection gates: ratio zero, or no started base.
for ratio, started in ((0, 1), (100, 0)):
    table = [0] * 250
    table[0], table[1] = 127, 99  # reject once, then return 100
    cpu.mem_write(ring, state(table, 0, 103))
    word(house + 0x554c, ratio)
    cpu.mem_write(house + 0x13a, bytes([started]))
    cpu.reg_write(UC_X86_REG_EDI, house)
    cpu.reg_write(UC_X86_REG_ESP, stack)
    cpu.emu_start(0x6bed92, 0x6bf089, count=10000)
    _, first, second = reference(table, 0, 103, 1, 100)
    assert bytes(cpu.mem_read(ring, 1012)) == state(table, first, second)
    checks += 1
assert bytes(cpu.mem_read(0x839218, 1012)) == static_ring
print(f'{checks} original base 1.006 AI random call-site checks pass')
