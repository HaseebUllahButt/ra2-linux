#!/usr/bin/env python3
"""Check 0045F8FF..0045FB0A against unmodified base x86 and its sqrt table.

Focused build (from repository root; no gameplay or full build):
  g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
    rebuild/src/weapons/homing_speed{,_test}.cpp -o rebuild/build/homing_speed_test
Run:
  RA2_GAME_DIR=/path/to/base/game python3 \
    rebuild/src/weapons/verify_homing_speed_x86.py rebuild/build/homing_speed_test

Requires Python Unicorn. Checks raw double bits and every tick's state.
The x86 square root helper is executed, never replaced or hooked.
Numeric scope: finite velocities with zero or normal float squared lengths.
"""
import hashlib
import itertools
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_FPCW

game = Path(os.environ['RA2_GAME_DIR']) / 'game.exe'
image = game.read_bytes()
assert image[0xba410:0xba41a] == bytes.fromhex('dd442404d81538a77900'), 'Requires base game.exe'
assert image[0x5f8ff:0x5f905] == bytes.fromhex('dd85e0000000')
cpu = Uc(UC_ARCH_X86, UC_MODE_32)
cpu.mem_map(0x400000, 0x800000)
cpu.mem_write(0x400000, image[:0x431000])
cpu.mem_map(0x1000000, 0x100000)
bullet, kind, stack = 0x1001000, 0x1003000, 0x10f0000
cpu.reg_write(UC_X86_REG_FPCW, 0xe7f)
empty = cpu.context_save()
def write_int(address, value):
    cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))

visits = {}
branches = (0x45f998, 0x45f985, 0x45f9bd, 0x45f9ea, 0x45fa57, 0x45faba, 0x45faf0, 0x4ba410)
def hook(uc, address, size, data):
    if address in branches:
        visits[address] = visits.get(address, 0) + 1
cpu.hook_add(UC_HOOK_CODE, hook)

# Equality, flag-clear boundaries, signed comparisons, odd and negative
# acceleration, sub-unit vectors, signed zero, and frame parity/wrap.
cases = []
vectors = [(0., 0., 0.), (-0., 0., -0.), (1., 0., 0.),
           (0.125, -0.0625, 0.03125), (3., 4., 0.), (10., 0., 0.),
           (0.5 - 2.**-24, 0., 0.), (0.5, 0., 0.), (0.5 + 2.**-24, 0., 0.),
           (38.5 - 2.**-17, 0., 0.), (38.5, 0., 0.), (38.5 + 2.**-17, 0., 0.),
           (39.5, 0., 0.), (39.49999999999999, 0., 0.),
           (39.50000000000001, 0., 0.), (40., 0., 0.),
           (0., 0., -41.), (17.125, -29.25, 3.875)]
for v, limit, flag, acc in itertools.product(vectors, (-1, 0, 1, 5, 39, 40, 41), (0, 1, 255), (-7, -1, 0, 1, 3, 8, 100)):
    cases.append((*v, limit, flag, 0, acc, 0, 0, 1))
for threshold in (-2147483648, -2, 1, 2, 10, 2147483647):
    counters = set((-2147483648, -1, 0, 1, 2147483647, threshold))
    if threshold > -2147483648:
        counters.add(threshold - 1)
    if threshold < 2147483647:
        counters.add(threshold + 1)
    for counter, frame in itertools.product(sorted(counters), (-2147483648, -3, -2, -1, 0, 1, 2, 2147483647)):
        cases.append((.375, -.625, .125, 39, 255, counter, 3, threshold, frame, 4))
for frame, limit, acc in itertools.product((-2147483648, -3, -2, -1, 0, 1, 2147483647), (1, 39, 40, 80), (1, 3, 8)):
    cases.append((.125, -.25, .5, limit, 1, 0, acc, 0, frame, 60))

rng = random.Random(0x45f8ff)
for _ in range(3500):
    # Continuous fractions expose truncation differences in multiply/add/divide.
    v = tuple(rng.uniform(-150, 150) * rng.choice((1., .001, 10000.)) for _ in range(3))
    cases.append((*v, rng.choice((-2147483648, -1, 0, 1, 10, 39, 40, 100, 2147483647)),
                  rng.choice((0, 1, 128, 255)), rng.randrange(-5, 15),
                  rng.choice((-2147483648, -7, -1, 0, 1, 3, 8, 100, 2147483647)),
                  rng.choice((-2, 0, 1, 2, 10)), rng.randrange(-20, 20), rng.choice((1, 2, 15))))
# Squared-length rounding boundaries across small and large normal exponents.
for e in (-60, -20, -1, 0, 20, 60):
    n = 2. ** e
    for x in (math.nextafter(n, 0.), n, math.nextafter(n, math.inf)):
        cases.append((x, -x / 3., x / 7., 39, 1, 0, 3, 4, 0, 6))

output = subprocess.check_output([sys.argv[1], str(game)], input=''.join(
    ' '.join(map(str, c)) + '\n' for c in cases), text=True).splitlines()
assert len(output) == sum(c[-1] for c in cases)
lines = iter(output)
ticks = 0
for case in cases:
    x, y, z, limit, flag, counter, acceleration, threshold, frame, count = case
    cpu.mem_write(bullet, bytes(0x200))
    cpu.mem_write(kind, bytes(0x300))
    cpu.mem_write(bullet + 0xd0, struct.pack('<ddd', x, y, z))
    cpu.mem_write(bullet + 0xed, bytes((flag,)))
    write_int(bullet + 0x98, kind)
    write_int(bullet + 0xf8, limit)
    write_int(bullet + 0xf0, counter)
    write_int(kind + 0x2a0, acceleration)
    write_int(kind + 0x2b0, threshold)
    write_int(kind + 0x2ac, 1)  # ROT > 0. Arcing +290 remains false.
    for tick in range(count):
        cpu.context_restore(empty)
        cpu.reg_write(UC_X86_REG_EBP, bullet)
        cpu.reg_write(UC_X86_REG_ESP, stack)
        cpu.mem_write(stack, bytes(0x200))
        write_int(0xa40d2c, frame + tick)
        before = bytes(cpu.mem_read(bullet, 0x200))
        cpu.emu_start(0x45f8ff, 0x45fb0b, count=10000)
        after = bytes(cpu.mem_read(bullet, 0x200))
        # Ensure the selected instruction range writes no other bullet fields.
        allowed = set(range(0xd0, 0xe8)) | {0xed} | set(range(0xf0, 0xf4))
        assert all(a == b or i in allowed for i, (a, b) in enumerate(zip(before, after)))
        actual = (*struct.unpack_from('<QQQ', after, 0xd0), after[0xed],
                  struct.unpack_from('<i', after, 0xf0)[0], struct.unpack_from('<i', after, 0xf8)[0])
        expected = tuple(map(int, next(lines).split()))
        assert actual == expected, (case, tick, actual, expected)
        ticks += 1
assert set(visits) == set(branches), visits

# Rejected values must fail explicitly, not produce plausible native output.
for v in ('1e100 0 0', '1e-30 0 0'):
    bad = subprocess.run([sys.argv[1], str(game)], input=f'{v} 39 1 0 3 0 0 1\n',
                         text=True, capture_output=True)
    assert bad.returncode != 0 and not bad.stdout, bad
wrong = subprocess.run([sys.argv[1], str(game.with_name('gamemd.exe'))], text=True, capture_output=True)
assert wrong.returncode != 0, 'Expansion executable must be rejected'
print(f'Base game.exe SHA256: {hashlib.sha256(image).hexdigest()}')
print(f'{len(cases)} cases; {ticks} ticks match all velocity bits, flag, counter, and limit')
print('Original sqrt runs unchanged; all selected branches run; unsupported ranges and expansion rejected')
