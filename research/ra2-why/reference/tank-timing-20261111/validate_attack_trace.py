import json,pathlib,csv
root=pathlib.Path('runs/kill-timing');summary=[]
for path in sorted(list(root.glob('*-detail-*-trace.jsonl'))+list(root.glob('*-counterfactual-*-trace.jsonl'))):
 d=[json.loads(x) for x in path.read_text().splitlines()];meta=d[0]['meta'];boundary=[x for x in d if x.get('kind')=='boundary'];events=[x for x in d if x.get('kind') in ['entry','point','return']];entries={x['call_id']:x for x in d if x.get('kind')=='entry'}
 assert [r['frame'] for r in boundary]==list(range(33))
 assert d[-1]=={'kind':'end','pending':[]}
 expected_hp=300 if path.name.startswith('grizzly') else 400
 for x in d:
  if 'shooter' not in x or 'target' not in x:continue
  s,t=x['shooter'],x['target'];assert s['hp']==expected_hp and t['rearm_start']==-1 and t['rearm_duration']==0 and t['target']=='0x0'
  assert s['xyz']==[22144,16768,208] and t['xyz']==[23168,16768,208]
 def points(name,frame=None):return [e for e in events if e.get('function')==name and e['kind']=='point' and (frame is None or e['frame']==frame)]
 returns=[e for e in events if e.get('function')=='unit_can_fire' and e['kind']=='return']
 shots=points('reload_store');assert len(shots)==1
 normal='-detail-' in path.name
 assert shots[0]['frame']==(13 if normal else 11)
 assert boundary[shots[0]['frame']+1]['shooter']['rearm_start']==meta['base_frame']+shots[0]['frame']
 if normal:
  for frame in [11,12]:
   checks=[e for e in returns if e['frame']==frame];assert checks and all(e['eax']==4 for e in checks)
   assert points('rotation_gate',frame)[0]['flag_599']==1
   assert points('return_rotating',frame)
  assert points('after_is_rotating',11)[0]['registers']['rax']&255==1
  assert points('after_is_rotating',12)[0]['registers']['rax']&255==0
  assert points('aim_done',12)[0]['flag_599']==0
  assert points('rotation_gate',13)[0]['flag_599']==0
  assert points('facing_compare',13)[0]['registers']['rax']==0
 else:
  intervention=[x for x in d if x.get('kind')=='intervention'];assert len(intervention)==1 and intervention[0]['frame']==11 and intervention[0]['before']==1 and intervention[0]['after']==0
  assert points('facing_compare',11)[0]['registers']['rax']==1365
  assert points('facing_compare',11)[0]['registers']['rcx']==2048
 facings={}
 for e in events:
  if e.get('function')=='facing_current' and e['kind']=='return':
   entry=entries[e['call_id']]
   if entry['object']==hex(int(meta['shooter'],16)+0x2f8):facings.setdefault(e['frame'],set()).add(e['current'])
 assert facings[11]=={15018} and facings[12]=={16383}
 restored=json.loads(path.with_name(path.name.replace('-trace.jsonl','-restored.json')).read_text());assert restored['0x54068a']=='74'
 row=dict(name=meta['label'],base_frame=meta['base_frame'],shot_update=shots[0]['frame'],shot_boundary=shots[0]['frame']+1,first_hit_boundary=next(x['frame'] for x in boundary if x['target']['hp']<expected_hp),attack_calls=[e['frame'] for e in events if e.get('function')=='foot_attack' and e['kind']=='entry'],attack_results=[dict(frame=e['frame'],result=e['eax']) for e in events if e.get('function')=='foot_attack' and e['kind']=='return'],can_fire=[dict(frame=e['frame'],result=e['eax'],caller=hex(entries[e['call_id']]['stack'][0])) for e in returns],restored_points=len(restored))
 summary.append(row);print(row['name'],'shot update',row['shot_update'],'boundary',row['shot_boundary'],'hit',row['first_hit_boundary'],'restored',len(restored))
 with path.with_suffix('.csv').open('w') as out:
  w=csv.writer(out);w.writerow(['kind','frame','function','result','flag_599','flag_57d','address','caller','current','compare_difference','compare_threshold'])
  for x in d:
   if x.get('kind') not in ['boundary','entry','point','return','intervention']:continue
   regs=x.get('registers',{});fn=x.get('function','');w.writerow([x['kind'],x['frame'],fn,x.get('eax',''),x.get('flag_599',''),x.get('flag_57d',''),x.get('address',''),hex(x['stack'][0]) if 'stack' in x else '',x.get('current',''),regs.get('rax','') if fn=='facing_compare' else '',regs.get('rcx','') if fn=='facing_compare' else ''])
(root/'attack-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('PASS: six normal traces; two separate field-change experiments; consecutive boundaries; fixed positions; no return fire; restored code; exact turning-gate cause.')
