"""Merge the decompile index with community-documented names.

Reads:
  rebuild/decomp/index.csv        (address,name,size,ok)
  analysis/community-crosscheck.md (address | name tables)

Writes rebuild/symbols.csv: address, ghidra_name, size, decompiled,
community_name, source.
"""

import csv
import os
import re

ROOT = '/path/to/user/ra2'


def parse_community(path):
    names = {}
    if not os.path.exists(path):
        return names
    text = open(path, encoding='utf-8').read()
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith('|'):
            continue
        cells = [c.strip() for c in line.strip('|').split('|')]
        if len(cells) < 2:
            continue
        addr_cell = cells[0].strip('`')
        m = re.match(r'^(0x)?([0-9A-Fa-f]{8})$', addr_cell)
        if not m:
            continue
        addr = m.group(2).upper()
        name = cells[1].strip('`')
        if name and name != 'Name' and not name.startswith('-'):
            names.setdefault(addr, name)
    return names


def main():
    index_path = os.path.join(ROOT, 'rebuild/decomp/index.csv')
    out_path = os.path.join(ROOT, 'rebuild/symbols.csv')
    community = parse_community(
        os.path.join(ROOT, 'analysis/community-crosscheck.md'))
    rows = list(csv.DictReader(open(index_path)))
    out = csv.writer(open(out_path, 'w', newline=''))
    out.writerow(['address', 'ghidra_name', 'size', 'ok',
                  'community_name'])
    named = 0
    for row in rows:
        addr = row['address'].upper()
        name = community.get(addr, '')
        if name:
            named += 1
        out.writerow([addr, row['name'], row['size'], row['ok'], name])
    print("functions done")


main()
