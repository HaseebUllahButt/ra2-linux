#!/usr/bin/env python3
"""Compare every frame's health and validate the stationary attack fixture."""
import json,sys
from pathlib import Path
native=Path(sys.argv[1]);reference=Path(sys.argv[2]);summary=[]
for name in ('gi-1','gi-2','gi-3','conscript-1','conscript-2','conscript-3'):
 def load(root):return [json.loads(x) for x in (root/(name+'.jsonl')).read_text().splitlines()][1:]
 rows=load(native);orig=load(reference)
 assert [r['frame'] for r in rows]==list(range(len(rows))),name+' missing frame'
 assert rows[-1]['target']['hp']==0,name+' incomplete kill'
 for r in rows:
  assert r['shooter']['hp']==125,name+' shooter damaged'
  for obj in ('shooter','target'):assert r[obj]['xyz']==rows[0][obj]['xyz'],name+' movement'
  assert r['target']['xyz']==[r['shooter']['xyz'][0]+512,*r['shooter']['xyz'][1:]],name+' separation'
  assert r['shooter']['owner']==r['target']['owner'],name+' owner'
 def hits(rs):return [{'frame':r['frame'],'hp':r['target']['hp'],'damage':rs[i-1]['target']['hp']-r['target']['hp'],'reload':r['shooter']['rearm_duration']} for i,r in enumerate(rs) if i and r['target']['hp']!=rs[i-1]['target']['hp']]
 h=hits(rows);oh=hits(orig);gaps=[b['frame']-a['frame'] for a,b in zip(h,h[1:])];extra=6 if name.startswith('conscript') else 2
 assert gaps==[x['reload']+extra for x in h[:-1]],name+' unexplained gap'
 assert h[0]['frame']==(7 if name.startswith('conscript') else 3),name+' first hit'
 assert len(h)==(18 if name.startswith('conscript') else 12),name+' hit count'
 first=next(({'frame':r['frame'],'original_shooter_hp':o['shooter']['hp'],'native_shooter_hp':r['shooter']['hp'],'original_target_hp':o['target']['hp'],'native_target_hp':r['target']['hp']} for r,o in zip(rows,orig) if (r['shooter']['hp'],r['target']['hp'])!=(o['shooter']['hp'],o['target']['hp'])),None)
 result={'name':name,'native_kill_frame':rows[-1]['frame'],'original_kill_frame':orig[-1]['frame'],'first_hit':h[0]['frame'],'hit_count':len(h),'gaps':gaps,'damage_matches_by_hit':[x['damage'] for x in h]==[x['damage'] for x in oh],'first_hp_difference':first,'native_positions':{o:rows[0][o]['xyz'] for o in ('shooter','target')},'original_positions':{o:orig[0][o]['xyz'] for o in ('shooter','target')},'hits':h}
 result['first_reload_difference']=next(({'frame':a['frame'],'native':a['shooter']['rearm_duration'],'original':b['shooter']['rearm_duration']} for a,b in zip(rows,orig) if a['shooter']['rearm_duration']!=b['shooter']['rearm_duration']),None)
 summary.append(result)
 print(name,'native/original kill',result['native_kill_frame'],result['original_kill_frame'],'first HP difference',None if first is None else first['frame'],'gaps',min(gaps),max(gaps))
(native/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('PASS: six complete stationary attacks; full shooter health; correct separation, owners, first hits, hit counts, and reload gaps.')
