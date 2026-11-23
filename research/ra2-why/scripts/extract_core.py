"""Extract spawn/map lists and core INIs from RA2 mixes, then resolve
map names in multim/md/maps mixes and extract everything found.

Outputs under ~/ra2/analysis/extracted/
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mixlib

GAME = os.path.expanduser("~/ra2/game")
OUT = os.path.expandhome("~/ra2/analysis/extracted")

MIXES = ["ra.mix", "ra2md.mix", "langmd.mix", "language.mix",
         "expandmd01.mix", "multimd.mix", "mapsmd03.md", "thememd.mix"]


def load_mixes():
    mixes = {}
    for name in MIXES:
        path = os.path.join(GAME, name)
        if os.path.exists(path):
            mixes[name] = mixlib.MixFile(path)
    return mixes


def find_entry(mix, filename):
    entry = mix.name_for(filename)
    return entry


def main():
    os.makedirs(OUT, exist_ok=True)
    mixes = load_loads()

    # 1. dump every entry's first bytes to classify by magic
    kinds = {}
    for name, mix in mixes.items():
        for entry in mix.entries:
            blob = mix.read(entry)
            magic = blob[:4]
            kind = "unknown"
            if blob.startswith(b"XCC "):
                kind = "xcc-db"
            elif magic == b"BIKi":
                kind = "bik"
            elif blob[:4] == b"RIFF":
                kind = "wav"
            elif blob[:2] == b"BM":
                kind = "bmp"
            elif blob[:4] in (b"\x89PNG",):
                kind = "png"
            elif b"\r\n[" in blob[:4096] or blob[:1] == b"[":
                kind = "ini"
            elif magic == b"\x00\x00\x01\x00" or magic[:2] == b"\x00\x00":
                kind = "maybe-mix"
            kinds[(name, entry.hash)] = kind
    return mixes, kinds


if __name__ == "__main__":
    mixes, kinds = main()
    from collections import Counter
    counts = Counter(kinds.values())
    print(counts)
