#!/usr/bin/env python3
"""Add or remove the JOIN in-process frame sampler in this private generated game copy.

The sampler is a read-only observer.  It adds:

  * one call at the frame boundary (instruction 0x0054068A, the branch that
    follows the game frame counter increment) in every emitted copy of that
    boundary;
  * one call at each health write of the damage method VoxelAnimClass__virtual_86
    (0x005D3DC7, 0x005D3E65 and 0x005D3F11);
  * the sampler source file to the game target.

--remove puts every file back exactly as it was.
"""
from pathlib import Path
import os
import re, shutil, sys

root = Path(os.environ.get('RA2_SOURCE_DIR', Path(__file__).resolve().parents[1])).resolve()
here = Path(__file__).resolve().parent
remove = '--remove' in sys.argv

FRAME_NEEDLE = '    if (CMP_EQ(_flag_a, _flag_b)) goto L_0054069D; /* 0x0054068A: je 0x54069d */'
FRAME_LINE = '    RECOMP_REGS_OUT(); join_sampler_frame({n}); RECOMP_REGS_IN();\n'
PROTO = ('extern void join_sampler_frame(int site);\n'
         'extern void join_sampler_damage(unsigned int target, unsigned int attacker, int old_health, int site);\n')

DMG_CALL = ('    {{ uint32_t _sm_t = esi; uint32_t _sm_a = MEM32(esp + 0x34); int _sm_o = (int)MEM32(esp + 0x14);\n'
            '      RECOMP_REGS_OUT(); join_sampler_damage(_sm_t, _sm_a, _sm_o, {n}); RECOMP_REGS_IN(); }}\n')

DMG1_OLD = ('    MEM32(esi + 0x6C) = eax; /* 0x005D3DC7: mov dword ptr [esi + 0x6c], eax */\n'
            '    if (CMP_LE(_flag_a, _flag_b)) goto L_005D3DCF; /* 0x005D3DCA: jle 0x5d3dcf */\n'
            'L_005D3DCC:\n'
            '    MEM32(esi + 0x6C) = edi; /* 0x005D3DCC: mov dword ptr [esi + 0x6c], edi */\n'
            'L_005D3DCF:\n')
DMG2_OLD = '    MEM32(esi + 0x6C) = eax; /* 0x005D3E65: mov dword ptr [esi + 0x6c], eax */\n'
DMG3_OLD = '    MEM32(esi + 0x6C) = eax; /* 0x005D3F11: mov dword ptr [esi + 0x6c], eax */\n'

frame_sites = 0
damage_sites = 0

for p in sorted((root / 'src/recomp/gen_game').glob('recomp_*.c')):
    s = p.read_text()
    touched = False
    fps_old = 'MEM32(0xA6EB1C) = eax; /* 0x00540BB5:'
    fps_new = 'MEM32(0xA6EB1C) = 20; /* 0x00540BB5:'
    s = s.replace(fps_new, fps_old) if remove else s.replace(fps_old, fps_new)
    if FRAME_NEEDLE in s:
        touched = True
        n = s.count(FRAME_NEEDLE)
        if remove:
            s = re.sub(r'^    RECOMP_REGS_OUT\(\); join_sampler_frame\(\d+\); RECOMP_REGS_IN\(\);\n', '', s, flags=re.M)
        else:
            # start from a clean file, so a second install does not double the hook
            s = re.sub(r'^    RECOMP_REGS_OUT\(\); join_sampler_frame\(\d+\); RECOMP_REGS_IN\(\);\n', '', s, flags=re.M)
            parts = s.split(FRAME_NEEDLE)
            out = parts[0]
            for i, part in enumerate(parts[1:]):
                out += FRAME_LINE.format(n=frame_sites + i) + FRAME_NEEDLE + part
            s = out
            frame_sites += n
    if DMG1_OLD in s or DMG2_OLD in s or DMG3_OLD in s:
        touched = True
        if remove:
            s = s.replace(DMG1_OLD + DMG_CALL.format(n=1), DMG1_OLD)
            s = s.replace(DMG2_OLD + DMG_CALL.format(n=2), DMG2_OLD)
            s = s.replace(DMG3_OLD + DMG_CALL.format(n=3), DMG3_OLD)
        else:
            # start from a clean file, so a second install does not double the hook
            s = re.sub(r'^    \{ uint32_t _sm_t = esi;.*\n      RECOMP_REGS_OUT\(\); join_sampler_damage\(.*\n', '', s, flags=re.M)
            if DMG1_OLD in s:
                s = s.replace(DMG1_OLD, DMG1_OLD + DMG_CALL.format(n=1)); damage_sites += 1
            if DMG2_OLD in s:
                s = s.replace(DMG2_OLD, DMG2_OLD + DMG_CALL.format(n=2)); damage_sites += 1
            if DMG3_OLD in s:
                s = s.replace(DMG3_OLD, DMG3_OLD + DMG_CALL.format(n=3)); damage_sites += 1
    if touched:
        s = s.replace(PROTO, '')
        if not remove:
            s = PROTO + s
    p.write_text(s)

if not remove:
    if not frame_sites:
        raise SystemExit('Frame boundary missing')
    if not damage_sites:
        raise SystemExit('Damage health writes missing')

p = root / 'CMakeLists.txt'
s = p.read_text()
if remove:
    s = s.replace('add_executable(ra2 src/linux/sampler_hook.c', 'add_executable(ra2')
else:
    if 'add_executable(ra2 src/linux/sampler_hook.c' not in s:
        s = s.replace('add_executable(ra2 src/linux/main.c', 'add_executable(ra2 src/linux/sampler_hook.c src/linux/main.c')
    shutil.copy2(here / 'sampler_hook.c', root / 'src/linux/sampler_hook.c')
p.write_text(s)
print(('Removed' if remove else 'Installed'), 'sampler:', frame_sites, 'frame boundaries,', damage_sites, 'damage writes')
