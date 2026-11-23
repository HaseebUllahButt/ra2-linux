"""Second-pass name resolution for RA2 MIX inventories.

Reads the CSV files from mix_inventory.py, tries generated candidate
names, then rewrites the CSV files and prints a report.
"""

import csv
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mixlib

OUTDIR = os.path.expanduser("~/ra2/analysis/mix-inventory")

BASES = [
    "cache", "conquer", "generic", "isogen", "isotemp", "isosnow",
    "isourb", "local", "neutral", "sounds", "score", "intro",
    "movies", "maps", "mult", "theme", "language", "load",
    "cameo", "audio", "mouse", "speech", "side", "util",
]

MD_BASES = [
    "cachemd", "conquermd", "genericmd", "isogenmd", "isotempmd",
    "isosnowmd", "isourbmd", "localmd", "neutralmd", "soundsmd",
    "scoremd", "intromd", "moviesmd", "mapsmd", "multmd",
    "thememd", "langmd", "loadmd", "cameomd", "audiomd",
    "mousemd", "speechmd", "sidemd", "utilmd",
]

STANDARD_FILES = [
    "rules.ini", "rulesmd.ini", "art.ini", "artmd.ini",
    "ai.ini", "aimd.ini", "sound.ini", "soundmd.ini",
    "ui.ini", "uimd.ini", "eva.ini", "evamd.ini",
    "spawn.ini", "spawnmd.ini", "mpmodes.ini", "mpmodesmd.ini",
    "theme.ini", "thememd.ini", "ra2.csf", "ra2md.csf",
    "tutorial.ini", "keyboard.ini", "sharelo.sno", "sharelo.tem",
    "sharelo.urb", "ealogo.pcx", "westwood.pcx", "logo.pcx",
    "snow.wst", "tem.wst", "urb.wt", "urb.wst",
]


def load_binary_names():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "binary_names.txt")
    names = set()
    if os.path.exists(path):
        for line in open(path, errors="ignore"):
            line = line.strip()
            if not line or "%" in line or "*" in line:
                continue
            line = line.replace("\\", "/").split("/")[-1]
            if "." in line and len(line) >= 5:
                names.add(line.upper())
    return names


def build_candidates():
    names = load_binary_names()
    for base in BASES:
        names.add(base + ".mix")
    for base in MD_BASES:
        names.add(base + ".mix")
    for name in STANDARD_FILES:
        names.add(name)
    game = os.path.expanduser("~/ra2/game")
    for pattern in ("*.mmx", "*.yro"):
        for path in glob.glob(os.path.join(game, pattern)):
            stem = os.path.splitext(os.path.basename(path))[0]
            names.add(stem + ".map")
    return names


def main():
    names = build_candidates()
    crc_map = {}
    classic_map = {}
    for name in names:
        upper = name.upper()
        crc_map.setdefault(mixlib.crc_hash(upper), upper)
        classic_map[mixlib.classic_hash(upper)] = upper
    report = []
    for path in sorted(glob.glob(os.path.join(OUTDIR, "*.csv"))):
        base = os.path.basename(path)
        with open(path) as handle:
            rows = list(csv.reader(handle))
        header = rows[0]
        body = rows[1:]
        with open(path, "w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(header)
            for row in body:
                name = row[1]
                if not name:
                    key = int(row[0], 16)
                    hit = crc_map.get(key)
                    if hit is None:
                        hit = classic_map.get(key)
                    if hit:
                        name = hit
                writer.writerow([row[0], name, row[2], row[3]])
            report.append((base, len(body)))
    for base, total in report:
        print("%-30s %5d entries" % (base, total))


if __name__ == "__main__":
    main()
