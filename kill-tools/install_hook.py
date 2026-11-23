#!/usr/bin/env python3
"""Add or remove the test hook in this private generated game copy."""
from pathlib import Path
import os
import sys,shutil
root=Path(os.environ.get('RA2_SOURCE_DIR', Path(__file__).resolve().parents[1])).resolve();here=Path(__file__).resolve().parent
remove='--remove' in sys.argv
needle='    if (CMP_EQ(_flag_a, _flag_b)) goto L_0054069D; /* 0x0054068A: je 0x54069d */'
hook='    RECOMP_REGS_OUT(); join_kill_frame(); RECOMP_REGS_IN();\n'
proto='extern void join_kill_frame(void);\n';count=0
for p in (root/'src/recomp/gen_game').glob('recomp_*.c'):
 s=p.read_text()
 if needle not in s:continue
 count+=s.count(needle)
 if remove:s=s.replace(proto,'').replace(hook,'')
 elif hook not in s:s=proto+s.replace(needle,hook+needle)
 p.write_text(s)
if not count:raise SystemExit('Frame boundary missing')
p=root/'CMakeLists.txt';s=p.read_text()
if remove:s=s.replace('add_executable(ra2 src/linux/kill_hook.c','add_executable(ra2')
else:
 if 'add_executable(ra2 src/linux/kill_hook.c' not in s:s=s.replace('add_executable(ra2 src/linux/main.c','add_executable(ra2 src/linux/kill_hook.c src/linux/main.c')
 shutil.copy2(here/'kill_hook.c',root/'src/linux/kill_hook.c')
p.write_text(s)
print(('Removed' if remove else 'Installed'),'hook at',count,'emitted frame boundaries')
