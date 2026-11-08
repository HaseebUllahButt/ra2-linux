import collections
import csv
import os

ROOT = '/path/to/user/repo2'
ROOT = '/path/to/user/ra2'


def read_csv(path):
    with open(path) as handle:
        return list(csv.DictReader(handle))


def read_edges():
    path = os.path.join(ROOT, 'analysis', 'ghidra-dump', 'call_edges.csv')
    out = []
    for row in read_csv(path):
        out.append((row['caller'].upper(), row['callee'].upper()))
    return out


def build_stats(funcs, symbols, edges):
    by_addr = {f['address'].upper(): f for f in funcs}
    named = sum(1 for a in by_addr if symbols.get(a, {}).get('name'))
    ok = sum(1 for f in funcs if str(f['ok']).lower() == 'true')
    total = sum(int(f['size']) for f in funcs)
    out_edges = collections.Counter()
    in_edges = collections.Counter()
    for src, dst in edges:
        out_edges[src] += 1
        in_edges[dst] += 1
    return by_addr, named, ok, total, out_edges, in_edges


def name_of(addr, by_addr, symbols):
    s = symbols.get(addr, {}).get('name')
    if s:
        return s
    f = by_addr.get(addr)
    return f['name'] if f else '?'


def write_dashboard(funcs, symbols, edges):
    by_addr, named, ok, total, out_e, in_e = build_stats(
        funcs, symbols, edges)
    lines = []
    lines.append('# Rebuild dashboard')
    lines.append('')
    lines.append('Data: decomp index + call edges + symbols.')
    lines.append('')
    lines.append('| metric | value |')
    lines.append('|---|---|')
    lines.append('| functions | %d |' % len(funcs))
    lines.append('| decompiled ok | %d' % ok)
    lines.append('| code bytes | %d' % total)
    lines.append('| community-named | %d' % named)
    lines.append('')
    lines.append('## Largest functions')
    lines.append('')
    lines.append('| address | name | bytes |')
    lines.append('|---|---|---|')
    for f in sorted(funcs, key=lambda x: -int(x['size']))[:20]:
        a = f['address'].upper()
        lines.append('| %s | %s | %s |' % (
            a, name_of(a, by_addr, symbols), f['size']))
    lines.append('')
    append_hub(lines, 'Busiest callers (fan-out)', out_e,
               by_addr, symbols, 'calls out')
    append_hub(lines, 'Busiest callees (fan-in)', in_e,
               by_addr, symbols, 'called by')
    out = os.path.join(ROOT, 'rebuild', 'dashboard.md')
    with open(out, 'w') as handle:
        handle.write('\n'.join(lines) + '\n')
    print('dashboard written: %s' % out)


def append_hub(lines, title, counter, by_addr, symbols, label):
    lines.append('')
    lines.append('## %s' % title)
    lines.append('')
    lines.append('| address | %s | name |' % label)
    lines.append('|---|---|---|')
    for addr, count in counter.most_common(15):
        lines.append('| %s | %d | %s |' % (
            addr, count, name_of(addr, by_addr, symbols)))


def main():
    funcs = read_csv(os.path.join(ROOT, 'rebuild', 'decomp', 'index.csv'))
    try:
        symbols = {r['address']: r for r in read_csv(
            os.path.join(ROOT, 'rebuild', 'symbols-extended.csv'))}
    except FileNotFoundError:
        symbols = {}
    edges = read_edges()
    write_dashboard(funcs, symbols, edges)


main()
