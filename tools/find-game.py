#!/usr/bin/env python3
"""Print local Red Alert 2 folder candidates, separated by NUL bytes."""

import argparse
import os
from pathlib import Path
import re
import sys

STEAM_APP_ID = "2229850"


def children(directory):
    try:
        return sorted(directory.iterdir())
    except OSError:
        return []


def text(path):
    try:
        return path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return ""


def vdf_values(path, field):
    pattern = r'^\s*"' + re.escape(field) + r'"\s*"((?:\\.|[^"\\])*)"'
    for match in re.finditer(pattern, text(path), re.MULTILINE | re.IGNORECASE):
        yield re.sub(r'\\(["\\])', r'\1', match[1])


def steam_libraries(home, data):
    roots = (data / "Steam", home / ".local/share/Steam", home / ".steam/steam",
             home / ".steam/root", home / ".steam/debian-installation",
             home / ".var/app/com.valvesoftware.Steam/.local/share/Steam")
    seen = set()
    for root in roots:
        if not (root / "steamapps").is_dir():
            continue
        extra = (Path(folder) for folder in vdf_values(root / "steamapps/libraryfolders.vdf", "path"))
        for library in (root, *extra):
            if library.is_absolute() and (library / "steamapps").is_dir():
                real = library.resolve()
                if real not in seen:
                    seen.add(real)
                    yield real


def game_folders(root, depth):
    """Find the game at the root or in a small number of subfolders."""
    entries = children(root)
    if any(p.name.casefold() == "game.exe" and p.is_file() for p in entries):
        yield root
    elif depth:
        for path in entries:
            if not path.is_symlink() and path.is_dir():
                yield from game_folders(path, depth - 1)


def title_folders(common):
    for path in children(common):
        title = re.sub(r"[^a-z0-9]+", " ", path.name.lower()).strip()
        if re.search(r"\bred ?alert ?(?:2|ii)\b", title) or title == "ra2":
            yield from game_folders(path, 2)
        elif "ultimate collection" in title:
            for part in children(path):
                part_title = re.sub(r"[^a-z0-9]+", " ", part.name.lower()).strip()
                if re.search(r"\bred ?alert ?(?:2|ii)\b", part_title) or part_title == "ra2":
                    yield from game_folders(part, 2)


def steam_folders(library):
    steamapps = library / "steamapps"
    common = steamapps / "common"
    for folder in vdf_values(steamapps / f"appmanifest_{STEAM_APP_ID}.acf", "installdir"):
        if folder not in ("", ".", "..") and Path(folder).name == folder:
            yield from game_folders(common / folder, 2)
    yield from title_folders(common)


def discover(home, data):
    for library in steam_libraries(home, data):
        yield from steam_folders(library)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path, nargs="?")
    args = parser.parse_args()
    home = Path.home()
    data = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    paths = game_folders(args.folder.expanduser(), 3) if args.folder else discover(home, data)
    seen = set()
    for candidate in paths:
        real = candidate.resolve()
        if real not in seen:
            seen.add(real)
            sys.stdout.buffer.write(os.fsencode(real) + b"\0")


if __name__ == "__main__":
    main()
