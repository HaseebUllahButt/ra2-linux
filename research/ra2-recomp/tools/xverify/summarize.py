#!/usr/bin/env python3
"""Check result counts and print the report tables."""
import json
from pathlib import Path
OUT=Path(__file__).resolve().parents[2]/'build/xverify'

def status(s):
    if not s:return 'cannot test'
    if s['differ']:return 'differ'
    if s['fault']:return 'same fault'
    if s['cannot']:return 'cannot test'
    return 'exact'

def summarize():
    manifest=json.loads((OUT/'manifest.json').read_text())
    results=json.loads((OUT/'results.json').read_text())
    assert len(results)==len(manifest['functions'])
    assert {f['addr'] for f in results}=={f['addr'] for f in manifest['functions']}
    output={}
    for group in ('x87','sample'):
        output[group]={}
        for side in ('ours','theirs'):
            rows=[f for f in results if f['set']==group]
            functions={k:sum(status(f.get(side))==k for f in rows) for k in ('exact','differ','same fault','cannot test')}
            states={k:sum(f[side][k] for f in rows if f.get(side)) for k in ('exact','differ','fault','cannot','return_differ','fault_differ')}
            precision={}
            for pc in ('pc64','pc53'):
                precision[pc]={k:sum(f[side][pc][k] for f in rows if f.get(side)) for k in ('exact','differ','fault','cannot')}
            for f in rows:
                s=f.get(side)
                if not s:continue
                assert sum(s[k] for k in ('exact','differ','fault','cannot'))==400,f['addr']
                assert s['differ']==s['return_differ']+s['fault_differ'],f['addr']
                for pc in ('pc64','pc53'):
                    assert sum(s[pc].values())==200,f['addr']
                for k in ('exact','differ','fault','cannot'):
                    assert s[k]==s['pc64'][k]+s['pc53'][k],f['addr']
            output[group][side]=dict(functions=functions,states=states,precision=precision,
                return_difference_functions=sum(f[side]['return_differ']>0 for f in rows if f.get(side)),
                fault_difference_functions=sum(f[side]['fault_differ']>0 for f in rows if f.get(side)))
    return output

if __name__=='__main__':print(json.dumps(summarize(),indent=2))
