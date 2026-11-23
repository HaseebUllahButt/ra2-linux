#!/usr/bin/env python3
"""Build the private JOIN binary build/ra2-orders. The release tree is only read.

1. Copy recomp_0021.c and recomp_0025.c from the release generated sources into build/join/.
2. Put one hook call before each needed guest instruction (every emitted copy):
     0x005158C4 seed read, 0x00540643 call Queue_AI, 0x0054068A frame boundary;
   replace the measured-fps store at 0x00540BB5 by the constant 20.
3. Compile those two files and src/orders_join.c with the release compile flags.
4. Link with the release link command: the same objects, the two patched objects in place of the
   release ones, plus orders_join.o.
"""
import hashlib, json, shlex, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
R = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERE.parents[1]
BD = R / 'build-linux-game'
GEN = R / 'src/recomp/gen_game'
OUT = HERE / 'build' / 'join'
OUT.mkdir(parents=True, exist_ok=True)
NINJA = 'ninja'

PROTO = 'extern void join_orders_seed(void);\nextern void join_orders_queue(void);\nextern void join_orders_boundary(void);\n'
EDITS = {
    'recomp_0021.c': [
        ('    eax = MEM32(0xA40D40); /* 0x005158C4: mov eax, dword ptr [0xa40d40] */',
         '    RECOMP_REGS_OUT(); join_orders_seed(); RECOMP_REGS_IN();\n', 1),
    ],
    'recomp_0025.c': [
        ('    RECOMP_CALL(sub_00623770); /* 0x00540643: call 0x623770 */',
         '    RECOMP_REGS_OUT(); join_orders_queue(); RECOMP_REGS_IN();\n', 16),
        ('    if (CMP_EQ(_flag_a, _flag_b)) goto L_0054069D; /* 0x0054068A: je 0x54069d */',
         '    RECOMP_REGS_OUT(); join_orders_boundary(); RECOMP_REGS_IN();\n', 16),
    ],
}
FPS_OLD = 'MEM32(0xA6EB1C) = eax; /* 0x00540BB5:'
FPS_NEW = 'MEM32(0xA6EB1C) = 20; /* 0x00540BB5:'

cmds = subprocess.run([NINJA, '-t', 'commands', 'ra2'], cwd=BD, check=True, capture_output=True, text=True).stdout.splitlines()
link = cmds[-1]
assert ' -o ra2 ' in link or link.rstrip().endswith('-o ra2') or ' -o ra2' in link, 'link command not found'
proof = {'release_sha256': hashlib.sha256((BD / 'ra2').read_bytes()).hexdigest(), 'files': {}}


def compile_like(src_name, src_path, obj):
    a = next(args for cmd in cmds if (args := shlex.split(cmd))[-1] == str(GEN / src_name))
    a[a.index('-o') + 1] = str(obj)
    a[a.index('-MF') + 1] = str(obj) + '.d'
    a[a.index('-MT') + 1] = str(obj)
    a[a.index('-c') + 1] = str(src_path)
    subprocess.run(a, cwd=BD, check=True)
    return a


for name, edits in EDITS.items():
    s = (GEN / name).read_text()
    counts = {}
    for needle, hook, want in edits:
        n = s.count(needle)
        assert n == want, f'{name}: {n} copies of {needle!r}, want {want}'
        s = s.replace(needle, hook + needle)
        counts[needle[:60]] = n
    if FPS_OLD in s:
        counts['fps'] = s.count(FPS_OLD)
        s = s.replace(FPS_OLD, FPS_NEW)
    s = PROTO + s
    (OUT / name).write_text(s)
    proof['files'][name] = {'release_sha256': hashlib.sha256((GEN / name).read_bytes()).hexdigest(), 'edits': counts}
    compile_like(name, OUT / name, OUT / (name + '.o'))

# the hook source: compile with the main.c flags
a = next(args for cmd in cmds if (args := shlex.split(cmd))[-1] == str(R / 'src/linux/main.c'))
obj = OUT / 'orders_join.o'
a[a.index('-o') + 1] = str(obj); a[a.index('-MF') + 1] = str(obj) + '.d'; a[a.index('-MT') + 1] = str(obj)
a[a.index('-c') + 1] = str(HERE / 'src/orders_join.c')
a.insert(a.index('-c'), '-I' + str(HERE / 'src'))
subprocess.run(a, cwd=BD, check=True)

# link
link_args = shlex.split(link.removeprefix(': && ').removesuffix(' && :'))
for name in EDITS:
    position = next(i for i, arg in enumerate(link_args) if arg.endswith('/' + name + '.o'))
    link_args[position] = str(OUT / (name + '.o'))
for i, arg in enumerate(link_args):
    if arg.startswith('-Wl,--dependency-file='):
        link_args[i] = '-Wl,--dependency-file=' + str(OUT / 'link.d')
link_args[link_args.index('-o') + 1] = str(HERE / 'build/ra2-orders')
link_args.insert(1, str(obj))
subprocess.run(link_args, cwd=BD, check=True)
proof['binary_sha256'] = hashlib.sha256((HERE / 'build/ra2-orders').read_bytes()).hexdigest()
after = hashlib.sha256((BD / 'ra2').read_bytes()).hexdigest()
assert after == proof['release_sha256'], 'release binary changed'
(HERE / 'build/join-build-proof.json').write_text(json.dumps(proof, indent=2))
print('built', HERE / 'build/ra2-orders', proof['binary_sha256'])
