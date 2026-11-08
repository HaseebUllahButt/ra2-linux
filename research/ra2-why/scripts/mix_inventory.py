"""Build an inventory of every MIX archive in the RA2 install.

Writes analysis/mix-inventory/<archive>.csv plus a SUMMARY.md.
Entry names are resolved by hashing candidate filenames with both
Westwood hash algorithms. Candidates come from the OpenRA mod tree,
the game folder, and a list of standard Westwood names.
"""

import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mixlib

GAME = os.path.expanduser("~/ra2/game")
MOD = os.path.expanduser("~/ra2/openra/ra2/mods/ra2")
OUTDIR = os.path.expanduser("~/ra2/analysis/mix-inventory")

BUILTIN_NAMES = """
RULES.INI RULESMD.INI ART.INI ARTMD.INI AI.INI AIMD.INI
SOUND.INI SOUNDMD.INI UI.INI UIMD.INI EVA.INI EVAMD.INI
SPAWN.INI SPAWNMD.INI MPMODES.INI MPMODESMD.INI THEME.INI THEMEMD.INI
CACHE.MIX LOCAL.MIX CONQUER.MIX GENERIC.MIX ISOGEN.MIX ISOTEMP.MIX
ISOSNOW.MIX ISOURB.MIX URBAN.MIX TEMPERAT.MIX SNOW.MIX
CAMEO.MIX CAMEOMD.MIX
SOUNDS.MIX SOUNDSMD.MIX THEME.MIX THEMEMD.MIX AUDIO.MIX AUDIOMD.MIX
MOVIES01.MIX MOVIES02.MIX MOVMD01.MIX MOVMD02.MIX MOVMD03.MIX
MAPS01.MIX MAPS02.MIX MAPS01.MIX MAPS_Ignore MAPSMD03.MIX
MULT.MIX MULTIMD.MIX LANGUAGE.MIX LANGMD.MIX
EXPAND01.MIX EXPANDMD01.MIX EXPANDMD02.MIX
ECACHE.MIX ELOCAL.MIX ECACHEMD.MIX ELOCALMD.MIX
LOAD.MIX LOADMD.MIX EALOGO.PCX WESTWOOD.PCX
INTRO.MIX INTROMD.MIX SCORE.MIX SCOREMD.MIX
""".split()


def gather_candidate_names():
    names = set()
    for name in BUILTIN_NAMES:
        names.add(name.strip().upper())
    pattern = re.compile(
        r"[\w\-. ]+?\.(?:shp|vxl|vpl|aud|wav|ini|map|mmx|yro|mix|pcx|bmp"
        r"|tga|csf|pal|hva|tem|sno|urb|uba|yrm|mpr|eva)\b",
        re.I,
    )
    for root in (MOD, GAME):
        for dirpath, _dirs, files in os.walk(root):
            for name in files:
                names.add(name.upper())
                lower = name.lower()
                if lower.endswith((".yaml", ".ini", ".txt", ".html", ".htm", ".xml")):
                    path = os.path.join(dirpath, name)
                    try:
                        with open(path, encoding="utf-8", errors="ignore") as handle:
                            text = handle.read()
                    except OSError:
                        continue
                    for match in pattern.finditer(text):
                        found = match.group(0).strip().strip("\"'")
                        names.add(found.upper())
    return sorted(names)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    candidates = gather_candidate_names()
    print("candidate names:", len(candidates))

    crc_map = {}
    classic_map = {}
    for name in candidates:
        crc_map.setdefault(mixlib.crc_hash(name), name)
        classic_map.setdefault(classic_hash := mixlib.classic_hash(name), name)

    archives = []
    for pattern in ("*.mix", "*.mmx", "*.yro"):
        archives.extend(glob.glob(os.path.join(GAME, pattern)))
    archives.sort()

    summary = []
    for path in archives:
        base = os.path.basename(path)
        try:
            mix = mixlib.MixFile(path)
        except Exception as exc:
            summary.append((base, -1, 0, "FAIL: " + str(exc)))
            print(base, "FAIL", exc)
            continue
        named = 0
        rows = []
        for entry in sorted(mix.entries, key=lambda item: item.hash):
            name = crc_map.get(entry.hash) or classic_map.get(entry.hash)
            if name:
                named += 1
            rows.append((entry.hash, name or "", entry.offset, entry.size))
        with open(os.path.join(OUTDIR, base + ".csv"), "w") as handle:
            handle.write("hash,name,offset_rel,size\n")
            for row in rows:
                handle.write("%08X,%s,%d,%d\n" % row)
        note = "enc=%d cnc=%d data@%d" % (mix.encrypted, mix.cnc_format, mix.data_start)
        summary.append((base, mix.count, named, note))
        print("%-24s n=%5d named=%5d %s" % (base, mix.count, named, note))

    with open(os.path.join(OUTDIR, "SUMMARY.md"), "w") as handle:
        handle.write("# MIX archive inventory\n")
        handle.write("\nEntry counts for every archive in the game folder.\n")
        handle.write("`named` counts entries whose hash matched a known filename.\n\n")
        handle.write("| archive | entries | named | notes |\n")
        handle.write("|---|---|---|---|\n")
        for base, count, named, note in summary:
            handle.write("| %s | %s | %s | %s |\n" % (base, count, named, note))


if __name__ == "__main__":
    main()
