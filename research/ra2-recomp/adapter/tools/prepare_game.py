#!/usr/bin/env python3
"""Stage an isolated kit. Never overwrite a link to an original game file."""
import argparse
import os
import shutil
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,default=Path('/path/to/user/Projects/ra2/original/game'))
p.add_argument('--bink',action='store_true')
a=p.parse_args()
destination=root/'build/game'; destination.mkdir(parents=True,exist_ok=True)
for source in a.source.iterdir():
 if not source.is_file() or source.name.lower() in ('ddraw.dll','dsound.dll','ra2adapter.dll','sdl2.dll'): continue
 if source.suffix.lower() in ('.log','.dmp','.sav'): continue
 target=destination/source.name
 if target.exists() or target.is_symlink(): continue
 if source.suffix.lower() in ('.ini','.exe'): shutil.copy2(source,target)
 else: target.symlink_to(source)
for name in ('ddraw.dll','dsound.dll','ra2adapter.dll','SDL2.dll'):
 target=destination/name
 if target.is_symlink(): target.unlink()
 shutil.copy2(root/'build'/name,target)
if a.bink:
 for target in destination.iterdir():
  if target.name.lower()=='binkw32.dll': target.unlink()
 shutil.copy2(root/'build/binkw32.dll',destination/'BINKW32.DLL')
print('Isolated game kit:',destination)
