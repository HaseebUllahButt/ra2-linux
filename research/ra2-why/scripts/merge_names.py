"""Merge resolved map and theme names into the mix inventory CSVs.

Reads map_names.csv (archive,hash,mapname) and updates the matching
mix inventory CSVs in place.
"""
import csv
import os

INV = '/path/to/user/mix-inventory'
INV = '/path/to/user/ra2/analysis/mix-inventory'
MAPS = '/path/to/user/ra2/analysis/map_names.csv'

extra = {}
with open(MAPS) as handle:
    for row in csv.DictReader(handle):
        if row['mapname']:
            extra[(row['archive'], row['hash'])] = row['mapname']

count = 0
for name in sorted(os.listdir(INV)):
    if not name.endswith('.csv') or name == 'SUMMARY.md':
        continue
    path = os.path.join(INV, name)
    rows = list(csv.reader(open(path)))
    header, body = rows[0], rows[1:]
    archive = name[:-4]
    changed = False
    for row in body:
        if not row[1]:
            hit = extra.get((archive, row[0]))
            if hit:
                row[1] = hit
                changed = True
                count += 1
    if changed:
        with open(path, 'w', newline='') as handle:
            writer = csv.writer(handle)
            writer.writerow(header)
            writer.writerows(body)
print('added names:', count)
