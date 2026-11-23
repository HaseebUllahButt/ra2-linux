import glob
import os

for path in sorted(glob.glob('/path/to/user/ra2/analysis/extracted/ini/*.ini')):
    with open(path, 'rb') as handle:
        blob = handle.read(2000)
    label = ''
    for line in blob.split(b'\n')[:8]:
        line = line.strip()
        if line.startswith(b';') and len(line) > 8:
            label = line[:60].decode('cp1252', 'replace')
            break
        if line.startswith(b'['):
            label = line.decode('cp1252', 'replace')
            break
    size = os.path.getsize(path)
    print('%-42s %9d  %s' % (os.path.basename(path), size, label))
