"""Resolve map names from the official spawn list against MIX entries.

Reads the MultiMaps spawn INI (langmd.mix entry 480B82E4), then tries
each map name (bare and with .MAP) against every entry hash in the
map-bearing archives. Writes map_names.csv and prints a report.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mixlib

GAME = "/path/to/user/ra2/game"
OUT = "/path/to/user/ra2/analysis"

SPAWN_HASH = 0x480B82E4
MAP_MIXES = ["multimd.mix", "expandmd01.mix", "mapsmd03.mix", "ra2md.mix"]


def read_spawn_names():
    mix = mixlib.MixFile(os.path.join(GAME, "langmd.mix"))
    entry = mix.hashes.get(SPAWN_HASH)
    if entry is None:
        raise SystemExit("spawn ini not found in langmd.mix")
    text = mix.read(entry).decode("ascii", "replace")
    names = []
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r"^\d+\s*=\s*(\S+)$", line)
        if m:
            names.append(m.group(1))
    return names


def main():
    names = read_spawn_names()
    print("spawn list maps:", len(names))

    candidates = {}
    for name in names:
        upper = name.upper()
        for variant in (upper, upper + ".MAP", upper + ".MPR"):
            candidates.setdefault(mixlib.crc_hash(variant), (upper, variant))
            candidates.setdefault(mixlib.classic_hash(variant), (upper, variant))

    rows = []
    for mix_name in MAP_MIXES:
        path = os.path.join(GAME, mix_name)
        if not os.path.exists(path):
            continue
        mix = mixlib.MixFile(path)
        for entry in mix.entries:
            hit = candidates.get(entry.hash)
            label = hit[1] if hit else ""
            rows.append((mix_name, entry.hash, entry.size, label))

    out = os.path.join(OUT, "map_names.csv")
    with open(out, "w") as handle:
        handle.write("archive,hash,size,mapname\n")
        for row in rows:
            handle.write("%s,%08X,%d,%s\n" % row)

    named = sum(1 for r in rows if r[3])
    print("entries checked: %d, resolved: %d" % (len(rows), named))
    for mix_name in MAP_MIXES:
        subset = [r for r in rows if r[0] == mix_name]
        named_sub = sum(1 for r in subset if r[3])
        print("  %-10s %3d entries, %3d named" % (mix_name, len(subset), named_sub))


main()
