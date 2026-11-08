"""Extract sample entries from MIX archives and peek at their bytes."""

import os
import sys

sys.path.insert(0, os.dirname(os.path.abspath(__file__)) if False else os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mixlib

GAME = os.path.expanduser("~/ra2/game")

TARGETS = [
    ("ra2md.mix", "8C8CCA19"),
    ("ra2md.mix", "3BFB683C"),
    ("ra2md.mix", "763C81DD"),
    ("langmd.mix", "480B82E4"),
("langmd.mix", "33665128"),
    ("expandmd01.mix", "B91D69A0"),
]

BROWSE = ("multimd.mix", "mapsmd03.mix", "thememd.mix", "movmd03.mix")


def describe(blob):
    head = blob[:16].hex()
    text = "".join(chr(b) if 32 <= b < 127 else "." for b in blob[:64])
    return head, text


def main():
    for mix_name, hash_hex in TARGETS:
        path = os.path.join(GAME, mix_name)
        mix = mixlib.MixFile(path)
        wanted = int(hash_hex, 16)
        for entry in mix.entries:
            if entry.hash == wanted:
                blob = mix.read(entry)
                head, text = describe(blob)
                print("%s %08X size=%d" % (mix_name, entry.hash, entry.size))
                print("  hex: %s" % head)
                print("  txt: %s" % text)
    for mix_name in BROWSE:
        path = os.path.join(GAME, mix_name)
        mix = mixlib.MixFile(path)
        print("== %s (%d entries)" % (mix_name, len(mix.entries)))
        for entry in sorted(mix.entries, key=lambda x: -x.size)[:5]:
            blob = mix.read(entry)
            head, text = describe(blob)
            print("  %08X size=%d hex=%s txt=%s" % ( entry.hash, entry.size, head, text))


main()
