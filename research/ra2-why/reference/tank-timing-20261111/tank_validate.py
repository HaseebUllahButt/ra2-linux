import json,pathlib
root=pathlib.Path('runs/kill-timing');results=[]
for name in [f'{kind}-{n}' for kind in ['grizzly','rhino'] for n in range(1,4)]:
 data=[json.loads(x) for x in (root/(name+'.jsonl')).read_text().splitlines()];meta=data[0]['meta'];rows=data[1:];hpmax=300 if name.startswith('grizzly') else 400;hp=hpmax;shots=[];hits=[]
 assert [r['frame'] for r in rows]==list(range(len(rows))),name+' missing frames'
 assert [r['game_frame'] for r in rows]==list(range(meta['base_frame'],meta['base_frame']+len(rows)))
 for i,r in enumerate(rows):
  s,t=r['shooter'],r['target'];assert s['hp']==hpmax,name+' shooter damaged'
  assert s['xyz']==rows[0]['shooter']['xyz'] and t['xyz']==rows[0]['target']['xyz'],name+' movement'
  assert t['xyz']==[s['xyz'][0]+1024,s['xyz'][1],s['xyz'][2]]
  assert t['rearm_start']==-1 and t['rearm_duration']==0 and t['target']=='0x0' and t['mission']==13,name+' target fired'
  assert s['owner']==t['owner']
  if i and s['rearm_start']!=rows[i-1]['shooter']['rearm_start']:
   shots.append(dict(frame=r['frame'],stored_start_relative=s['rearm_start']-meta['base_frame'],duration=s['rearm_duration']))
  if t['hp']!=hp:
   assert t['hp']<hp;hits.append(dict(frame=r['frame'],damage=hp-t['hp'],hp=t['hp']));hp=t['hp']
 assert hp==0 and rows[-1]['target']['alive']==0,name+' incomplete kill'
 assert len(shots)==len(hits)==5
 gaps=[b['frame']-a['frame'] for a,b in zip(shots,shots[1:])]
 assert gaps==[a['duration'] for a in shots[:-1]],name+' reload gap'
 assert all(s['frame']==s['stored_start_relative']+1 for s in shots)
 transit=[h['frame']-s['frame'] for s,h in zip(shots,hits)]
 result=dict(name=name,base_frame=meta['base_frame'],first_shot=shots[0]['frame'],first_hit=hits[0]['frame'],kill=rows[-1]['frame'],shots=shots,hits=hits,shot_gaps=gaps,impact_delays=transit,shooter_xyz=rows[0]['shooter']['xyz'],target_xyz=rows[0]['target']['xyz'],frames=len(rows))
 results.append(result);print(name,'shot',result['first_shot'],'hit',result['first_hit'],'kill',result['kill'],'shot_frames',[s['frame'] for s in shots],'hit_frames',[h['frame'] for h in hits],'damage',[h['damage'] for h in hits],'gaps',gaps,'transit',transit)
(root/'summary.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: six consecutive frame traces; fixed positions; four cells; full shooter health; no return fire; five hits; reload gaps exact.')
