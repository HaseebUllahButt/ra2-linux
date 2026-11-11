#!/usr/bin/env python3
import contextlib
import importlib.util
import io
import json
import tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('compare',Path(__file__).resolve().parents[1]/'tools/compare_frames.py')
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
with tempfile.TemporaryDirectory() as tmp:
    left,right=Path(tmp)/'left',Path(tmp)/'right'
    for folder in (left,right):
        folder.mkdir()
        (folder/'frame.rgba').write_bytes(bytes((1,2,3,255)))
        (folder/'frames.json').write_text(json.dumps([dict(frame=i,width=1,height=1,file='frame.rgba') for i in range(20)]))
    with contextlib.redirect_stdout(io.StringIO()):
        mod.compare(left,right,20)
    (right/'frame.rgba').write_bytes(bytes((1,3,3,255)))
    try:
        mod.compare(left,right,20)
    except ValueError as e:
        assert 'x=0, y=0, channel=1' in str(e)
    else:
        raise AssertionError('Comparator missed one changed pixel channel')
    try:
        mod.compare(left,right,21)
    except ValueError as e:
        assert 'Need 21 frames' in str(e)
    else:
        raise AssertionError('Comparator accepted too few frames')
print('PASS: 20-frame comparison accepts equal bytes and rejects one changed channel or too few frames')
