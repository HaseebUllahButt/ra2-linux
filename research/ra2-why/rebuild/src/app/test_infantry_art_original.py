#!/usr/bin/env python3
"""Spot-check base RA2 facing and sequence delay data, without running the game.

EA RA/TD Shape_Number and YRpp SubSequenceStruct guide the frame layout.
The owned base game.exe supplies the actual direction and delay table values.
This check covers table data, not activity changes or complete original draws.
"""
import pathlib
import struct
import sys


def read_va(image, address, size):
    if image[:2] != b"MZ":
        raise ValueError("Expected owned base game.exe")
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Expected PE image")
    optional = pe + 24
    if struct.unpack_from("<H", image, optional)[0] != 0x10B:
        raise ValueError("Expected PE32 image")
    base = struct.unpack_from("<I", image, optional + 28)[0]
    sections = optional + struct.unpack_from("<H", image, pe + 20)[0]
    for index in range(struct.unpack_from("<H", image, pe + 6)[0]):
        section = sections + index * 40
        rva, raw_size, raw = struct.unpack_from("<III", image, section + 12)
        offset = address - base - rva
        if 0 <= offset and offset + size <= raw_size:
            result = image[raw + offset:raw + offset + size]
            if len(result) == size:
                return result
    raise ValueError("Original table is outside the image")


if len(sys.argv) != 2:
    raise SystemExit("Usage: test_infantry_art_original.py <owned-base-game.exe>")
original = pathlib.Path(sys.argv[1]).read_bytes()
directions = struct.unpack("<32I", read_va(original, 0x7A33F4, 128))
for facing, expected in ((0, 7), (64, 5), (128, 3), (192, 1), (255, 7)):
    actual = directions[((facing // 4 + 1) // 2) & 31]
    if actual != expected:
        raise AssertionError(("Original infantry facing", facing, actual, expected))
for name, index, expected in (("Ready", 0, 0), ("Walk", 3, 3), ("FireUp", 4, 1)):
    actual = read_va(original, 0x7A3474 + index * 4 + 3, 1)[0]
    if actual != expected:
        raise AssertionError(("Original infantry delay", name, actual, expected))
print("Base binary infantry tables passed: five facings and three sequence delays.")
