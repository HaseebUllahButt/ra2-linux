#!/usr/bin/env python3
"""List ABI slots from installed Wine headers. This is not a runtime trace."""
import re
from pathlib import Path
for header in ('ddraw', 'dsound'):
    text = Path(f'/usr/include/wine/windows/{header}.h').read_text()
    for name, body in re.findall(r'DECLARE_INTERFACE_\((\w+),IUnknown\)\s*\{(.*?)\n\};', text, re.S):
        methods = re.findall(r'STDMETHOD(?:_\([^,]+,\s*(\w+)\)|\((\w+)\))', body)
        print(f'\n{name} (candidate ABI; trace must confirm use):')
        for slot, pair in enumerate(methods):
            print(f'  {slot:2}  +0x{slot*4:02x}  {pair[0] or pair[1]}')
