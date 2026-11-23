#!/usr/bin/env python3
"""Compare shared cell links with base 0x54AB70 and 0x54AF70.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  rebuild/src/weapons/{occupant_order,occupant_order_test}.cpp -o /tmp/ra2_occupants
Run: RA2_GAME_DIR=/path/to/game python3 \
  rebuild/src/weapons/verify_occupant_x86.py /tmp/ra2_occupants
"""

import os
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(
    0x400000, (Path(os.environ["RA2_GAME_DIR"]) / "game.exe").read_bytes()[:0x431000]
)
u.mem_map(0x1000000, 0x100000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
u.mem_map(0x4000000, 0x100000)
base, stack, stop = 0x1000000, 0x20FFF00, 0x3000000
mapobj, typemeta, vtable, origin = (
    base + 0x1000,
    base + 0x3000,
    base + 0x5000,
    base + 0x7000,
)
celladdrs = [base + 0x10000 + i * 0x1000 for i in range(3)]
objaddrs = [base + 0x20000 + i * 0x2000 for i in range(5)]


def w(addr, v):
    u.mem_write(addr, struct.pack("<I", v & 0xFFFFFFFF))


def read(addr):
    return struct.unpack("<I", u.mem_read(addr, 4))[0]


def ret(cpu, value, args):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    cpu.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
    cpu.reg_write(UC_X86_REG_EIP, read(sp))
    cpu.reg_write(UC_X86_REG_ESP, sp + 4 + 4 * args)


typevalue = 0
footprint = []
footprint_ptr = 0
insert_sp = remove_sp = 0
finished = False


def hook(cpu, address, size, user):
    global footprint_ptr, insert_sp, remove_sp, finished
    if address == stop + 0x100:
        ret(cpu, typevalue, 0)
    elif address == stop + 0x110:
        ret(cpu, typemeta, 0)
    elif address == stop + 0x120:
        ret(cpu, 2, 0)
    elif address == stop + 0x130:
        packed = b"".join(struct.pack("<hh", c - 1, 0) for c in footprint)
        packed += struct.pack("<hh", 0x7FFF, 0x7FFF)
        cpu.mem_write(base + 0x90000, packed)
        # The callee pops its one argument. The output pointer remains for
        # the original 0x482E10 footprint copy routine.
        ret(cpu, base + 0x90000, 1)
    elif address == 0x472540:
        ret(cpu, 0, 1)
    elif address == 0x473B30:
        insert_sp = cpu.reg_read(UC_X86_REG_ESP)
    elif address == 0x473BB5:
        cpu.reg_write(UC_X86_REG_ESI, read(insert_sp - 0x20))
        cpu.reg_write(UC_X86_REG_EBP, read(insert_sp - 0x1C))
        cpu.reg_write(UC_X86_REG_EBX, read(insert_sp - 0x18))
        cpu.reg_write(UC_X86_REG_EIP, read(insert_sp))
        cpu.reg_write(UC_X86_REG_ESP, insert_sp + 12)
    elif address == 0x473D20:
        remove_sp = cpu.reg_read(UC_X86_REG_ESP)
    elif address == 0x473D87:
        cpu.reg_write(UC_X86_REG_EDI, read(remove_sp - 0x14))
        cpu.reg_write(UC_X86_REG_ESI, read(remove_sp - 0x10))
        cpu.reg_write(UC_X86_REG_EIP, read(remove_sp))
        cpu.reg_write(UC_X86_REG_ESP, remove_sp + 12)
    elif address == stop:
        finished = True
        cpu.emu_stop()


for addr in (
    stop + 0x100,
    stop + 0x110,
    stop + 0x120,
    stop + 0x130,
    0x472540,
    0x473B30,
    0x473BB5,
    0x473D20,
    0x473D87,
    stop,
):
    u.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)
w(mapobj + 0x138, 0x4000000)
u.mem_write(origin, struct.pack("<hh", 10, 10))
u.mem_write(typemeta + 0x205, b"\1")
for off, addr in (
    (0x2C, stop + 0x100),
    (0x84, stop + 0x110),
    (0x74, stop + 0x120),
    (0x100, stop + 0x130),
):
    w(vtable + off, addr)
for i, cell in enumerate(celladdrs):
    w(0x4000000 + (10 * 512 + 10 + i) * 4, cell)
for obj in objaddrs:
    w(obj, vtable)
    w(obj + 0x418, typemeta)

rng = random.Random(0x54AB70)
trials = []
# Explicitly exercise a shared link changing both cell lists.
trials.append(
    [
        ("A", 0, 1, 1, [1]),
        ("A", 0, 2, 1, [2]),
        ("A", 0, 3, 1, [1, 2]),
        ("R", 0, 3, 1, [1]),
        ("A", 0, 4, 6, [2, 3]),
        ("R", 0, 4, 6, [3, 2]),
    ]
)
# The original copies at most 32 entries, including its terminator.
trials.append([("A", 0, 1, 1, [4] * 31 + [1])])
trials.append([("A", 0, 1, 1, [4] * 30 + [1, 2])])
for _ in range(200):
    ops = []
    for j in range(12):
        ops.append(
            (
                rng.choice("AAR"),
                rng.randrange(2),
                rng.randrange(1, 6),
                rng.choice((1, 2, 6, 15)),
                rng.sample((1, 2, 3, 4), rng.randrange(1, 4)),
            )
        )
    trials.append(ops)
source = ""
for trial in trials:
    source += "Z\n"
    for op, layer, obj, kind, fp in trial:
        source += (
            f"{op} {layer} {obj} {kind} {len(fp)} " + " ".join(map(str, fp)) + "\n"
        )
lines = iter(
    subprocess.check_output([sys.argv[1]], input=source, text=True).splitlines()
)
count = overlaps = cycles = 0
for trial in trials:
    for cell in celladdrs:
        w(cell + 0xE4, 0)
        w(cell + 0xE8, 0)
    for obj in objaddrs:
        w(obj + 0x30, 0)
    for op_index, (op, layer, obj, kind, fp) in enumerate(trial):
        expected = tuple(map(int, next(lines).split()))
        footprint = fp
        typevalue = kind
        finished = False
        u.mem_write(objaddrs[obj - 1] + 0x79, bytes([layer]))
        w(stack, stop)
        w(stack + 4, origin)
        w(stack + 8, objaddrs[obj - 1])
        u.reg_write(UC_X86_REG_ECX, mapobj)
        u.reg_write(UC_X86_REG_ESP, stack)
        u.emu_start(0x54AB70 if op == "A" else 0x54AF70, 0, count=20000)
        # A cyclic list causes the original traversal to keep running.
        assert finished == (expected[0] == 0), (
            op,
            layer,
            obj,
            kind,
            fp,
            expected,
            hex(u.reg_read(UC_X86_REG_EIP)),
        )

        def ID(ptr):
            if not ptr:
                return 0
            return objaddrs.index(ptr) + 1

        actual = [0 if finished else 1]
        for cell in celladdrs:
            actual.extend((ID(read(cell + 0xE4)), ID(read(cell + 0xE8))))
        actual.extend(ID(read(obj + 0x30)) for obj in objaddrs)
        assert tuple(actual) == expected, (op, layer, obj, kind, fp, actual, expected)
        count += 1
        overlaps += len(fp) > 1
        cycles += not finished
        if not finished:
            # The native model leaves earlier footprint writes intact.
            # Consume the remaining native lines for this reset sequence.
            for _ in range(len(trial) - op_index - 1):
                next(lines)
            break
# Execute the original scenario caller, with loader bodies as recording stubs.
loader_order = []
loader_addresses = {0x706AE0: 0, 0x41A840: 1, 0x506960: 2, 0x44AB80: 3}


def loader_hook(cpu, address, size, user):
    if address in loader_addresses:
        loader_order.append(loader_addresses[address])
        ret(cpu, 0, 0)
    elif address == 0x481FB0:
        ret(cpu, 0, 0)
    elif address == 0x672160:
        ret(cpu, 0, 1)


for addr in (*loader_addresses, 0x481FB0, 0x672160):
    u.hook_add(UC_HOOK_CODE, loader_hook, begin=addr, end=addr)
u.reg_write(UC_X86_REG_ESP, stack)
u.emu_start(0x65F768, 0x65F7B0, count=1000)
expected_order = list(
    map(int, subprocess.check_output([sys.argv[1]], input="O\n", text=True).split())
)
assert loader_order == expected_order, (loader_order, expected_order)
print(
    f"Base game.exe: {count} footprint link operations match; {overlaps} overlap and {cycles} cyclic traversals; scenario loader order matches"
)
