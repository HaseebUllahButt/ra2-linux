import hashlib
exec(open('bin/kill_timing_calls.py').read())

def tank_sample(o,frame):
 b=m.read(o,0x7d0)
 def i(off):return struct.unpack_from('<i',b,off)[0]
 def face(off):
  return dict(desired=i(off)&65535,start=i(off+4)&65535,timer_start=i(off+8),timer_stopped=i(off+12),timer_duration=i(off+16),rot=i(off+20)&65535)
 start,stopped,duration=i(0x244),i(0x248),i(0x24c)
 return dict(type=type_id(m,i(0x5ac)),hp=i(0x6c),alive=b[0x7d],xyz=[i(0x88),i(0x8c),i(0x90)],owner=hex(i(0x1b4)),mission=i(0x98),queued_mission=i(0xa0),target=hex(i(0x228)),stage=i(0xe0),facing_2e0=face(0x2e0),facing_2f8=face(0x2f8),rearm_start=start,rearm_stopped=stopped,rearm_duration=duration,rearm_left=max(0,duration-(frame-start)) if start!=-1 else duration)

def tank_measure(label,tp,home,loc):
 attach()
 try:
  pair=[call(tp,0x70aa80,(home,)) for _ in range(2)]
  assert all(m.u32(o)==0x7addf8 for o in pair),pair
  for o,xyz in zip(pair,[loc,(loc[0]+1024,loc[1],loc[2])]):
   place(o,xyz)
   for off,v in [(0x98,13),(0xa0,-1),(0x228,0),(0x494,0),(0x244,-1),(0x248,0),(0x24c,0)]:m.write_i32(o+off,v)
   for off in [0x2e0,0x2f8]:
    for delta,v in [(0,0),(4,0),(8,-1),(12,0),(16,0)]:m.write_i32(o+off+delta,v)
  shooter,victim=pair
  assert m.i32(shooter+0x6c)==m.i32(victim+0x6c)==(300 if label.startswith('grizzly') else 400)
  m.write_i32(shooter+0x98,1);m.write_i32(shooter+0x228,victim)
  base=m.i32(FRAME_ADDR)
  meta=dict(label=label,pid=pid,display=98,base_frame=base,shooter=hex(shooter),target=hex(victim),exe_sha256=hashlib.sha256(open('game/game.exe','rb').read()).hexdigest(),method='Fresh tanks: UnitType CreateObject 0070AA80, constructor 006FA400; Limbo/Put via +D0/+D4; same human house; Attack mission+target at stopped 0054068A boundary; facings zero; four cells; no weapon code changes')
  with open('runs/kill-timing/'+label+'.jsonl','w',buffering=1) as log:
   log.write(json.dumps({'meta':meta})+'\n')
   for n in range(1001):
    fr=m.i32(FRAME_ADDR);row=dict(frame=fr-base,game_frame=fr,shooter=tank_sample(shooter,fr),target=tank_sample(victim,fr))
    log.write(json.dumps(row)+'\n')
    if row['target']['hp']<=0:break
    next_frame()
  print('RESULT',label,row,flush=True)
  for o in pair:
   m.write_i32(o+0x98,13);m.write_i32(o+0x228,0)
   if m.i32(o+0x6c)>0:call(o,m.u32(m.u32(o)+0xd0))
 finally:detach()
