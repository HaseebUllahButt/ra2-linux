"""Extract every map from the map-bearing mixes using resolved names.

Named entries   -> analysis/extracted/maps/<name>.map
Unnamed entries -> analysis/unnamed/<archive>_<hash>.bin
Also prints magic bytes of langmd/language binary entries (CSF check).
"""

import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mixlib

GAME = "/path/to/user/ra2/game"
OUT = "/path/to/user/ra2/analysis/extracted"
MAP_CSV = "/path/to/user/ra2/analysis/map_names.csv"


def main():
    rows = []
    with open(MAP_CSV) as handle:
        for row in csv.DictReader(handle):
            rows.append(row)

    maps_dir = os.path.join(OUT, "maps")
    unnamed_dir = os.path.join(OUT, "unnamed")
    os.makedirs(maps_dir, exist_ok=True)
    os.makedirs(unnamed_dir, exist_ok=True)

    caches = {}
    extracted_named = 0
    extracted_unnamed = 0
    for row in rows:
        archive = row["archive"]
        if archive not in caches:
            caches[archive] = mixlib.MixFile(os.path.join(GAME, archive))
        mix = caches[archive]
        entry = mix.hashes.get(int(row["hash"], 16))
        if entry is None:
            continue
        blob = mix.read(entry)
        if row["mapname"]:
            name = row["mapname"]
            if not name.lower().endswith((".map", ".mpr")):
                name = name + ".map"
            with open(os.path.join(maps_dir, name), "wb") as handle:
                handle.write(blob)
            extracted_named += 1
        else:
            filename = "%s_%08X.bin" % (archive, entry.hash)
            with open(os.path.join(unnamed_dir, filename), "wb") as handle:
                handle.write(blob)
            extracted_unnamed += 1

    print("named maps extracted:", extracted_named)
    print("unnamed entries saved:", extracted_unnamed)

    for mix_name in ("langmd.mix", "language.mix"):
        mix = mixlib.MixFile(os.path.join(GAME, mix_name))
        for entry in mix.entries:
            blob = mix.read(entry)
            if blob[:4] == b"BIKi" or blob[:4] == b"RIFF":
                continue
            print(mix_name, "%08X" % entry.hash, entry.size, blob[:8].hex(),
                    repr(blob[:12]))


main()
