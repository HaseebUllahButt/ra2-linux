exec(open('bin/tank_measure.py').read())
STATIC={
 0x707f50:('unit_attack','rcx'),0x4c44c0:('foot_attack','rcx'),
 0x705070:('unit_attack_work','rcx'),0x4c4c30:('foot_attack_work','rcx'),
 0x704c00:('unit_can_fire','rcx'),0x6c9260:('techno_can_fire','rcx'),
 0x704ed0:('unit_fire','rcx'),0x6fbc90:('unit_fire_update','rcx'),0x6ca890:('techno_fire','rcx'),
 0x6fb890:('unit_aim','rcx'),0x4b8be0:('facing_current','rcx'),
 0x704c41:('after_techno_can_fire','rsi'),0x704df0:('facing_gate','rsi'),
 0x704e7b:('facing_compare','rsi'),0x704e8e:('facing_pass','rsi'),
 0x6cb971:('reload_store','rsi'),0x704dbd:('rotation_gate','rsi'),0x704de4:('return_rotating','rsi'),0x6fb9fc:('after_is_rotating','rsi'),0x6fba17:('write_rotating','rsi'),0x6fba83:('aim_done','rsi'),0x4b8c90:('facing_rotating','rcx')}
FUNCTIONS={'foot_attack','unit_attack_work','foot_attack_work','unit_can_fire','techno_can_fire','unit_fire','techno_fire','unit_aim','facing_current','facing_rotating','unit_fire_update'}

def trace_attack(label,tp,home,loc=(22144,16768,208),frames=32,clear_at=None):
 attach();originals={};enabled=set();pending=[];entries=0
 def patch(addr,b):m.f.seek(addr);m.f.write(b)
 def enable(addr):
  if addr not in originals:originals[addr]=m.read(addr,1);assert len(originals[addr])==1 and originals[addr]!=b'\xcc',hex(addr)
  if addr not in enabled:patch(addr,b'\xcc');enabled.add(addr)
 def disable(addr):
  if addr in enabled:patch(addr,originals[addr]);enabled.remove(addr)
 def reg():r=Regs();ptr(12,0,ctypes.byref(r));return r
 def step():
  ptr(9);_,status=os.waitpid(pid,0)
  assert os.WIFSTOPPED(status) and os.WSTOPSIG(status)==5,('step',status)
 def state():
  frame=m.i32(FRAME_ADDR)
  return dict(frame=frame-base,game_frame=frame,shooter=tank_sample(shooter,frame),target=tank_sample(victim,frame),mission_words=[m.i32(shooter+x) for x in range(0x98,0xd0,4)],flag_57d=m.read(shooter+0x57d,1)[0],flag_599=m.read(shooter+0x599,1)[0])
 try:
  pair=[call(tp,0x70aa80,(home,)) for _ in range(2)];shooter,victim=pair
  for o,xyz in zip(pair,[loc,(loc[0]+1024,loc[1],loc[2])]):
   place(o,xyz)
   for off,v in [(0x98,13),(0xa0,-1),(0x228,0),(0x494,0),(0x244,-1),(0x248,0),(0x24c,0)]:m.write_i32(o+off,v)
   for off in [0x2e0,0x2f8]:
    for delta,v in [(0,0),(4,0),(8,-1),(12,0),(16,0)]:m.write_i32(o+off+delta,v)
  m.write_i32(shooter+0x98,1);m.write_i32(shooter+0x228,victim);base=m.i32(FRAME_ADDR)
  with open('runs/kill-timing/'+label+'-trace.jsonl','w',buffering=1) as out:
   def emit(x):out.write(json.dumps(x)+'\n')
   emit(dict(meta=dict(label=label,pid=pid,display=98,base_frame=base,shooter=hex(shooter),target=hex(victim),exe_sha256=hashlib.sha256(open('game/game.exe','rb').read()).hexdigest(),points={hex(k):v for k,v in STATIC.items()})))
   emit(dict(kind='boundary',**state()))
   for addr in list(STATIC)+[0x54068a]:enable(addr)
   # The first boundary instruction has not run yet. Execute its restored byte.
   disable(0x54068a);step();enable(0x54068a)
   while True:
    ptr(7);_,status=os.waitpid(pid,0)
    assert os.WIFSTOPPED(status),('exit',status)
    if os.WSTOPSIG(status)!=5:
     raise RuntimeError(('unexpected signal',status,hex(reg().rip)))
    r=reg();addr=r.rip-1
    assert addr in enabled,('unknown trap',hex(addr))
    disable(addr);r.rip=addr;ptr(13,0,ctypes.byref(r))
    if addr==0x54068a:
     emit(dict(kind='boundary',**state()))
     if clear_at is not None and m.i32(FRAME_ADDR)-base==clear_at:
      old=m.read(shooter+0x599,1)[0];m.f.seek(shooter+0x599);m.f.write(b'\0')
      emit(dict(kind='intervention',field='599',before=old,after=0,**state()))
     if m.i32(FRAME_ADDR)-base>=frames:break
    else:
     # Return sites can be shared. The last pending matching call owns its result.
     for i in range(len(pending)-1,-1,-1):
      item=pending[i]
      if item['return_address']==addr:
       pending.pop(i)
       result=dict(kind='return',function=item['function'],call_id=item['call_id'],address=hex(addr),frame=m.i32(FRAME_ADDR)-base,eax=r.rax&0xffffffff)
       if item['function']=='facing_current':result['current']=m.u32(r.rax)&65535
       emit(result);break
     if addr in STATIC:
      name,field=STATIC[addr];o=getattr(r,field)&0xffffffff
      belongs=o==shooter or (name in ['facing_current','facing_rotating'] and o in [shooter+0x2e0,shooter+0x2f8])
      if belongs:
       entries+=1
       emit(dict(kind='entry' if name in FUNCTIONS else 'point',function=name,call_id=entries,address=hex(addr),object=hex(o),registers={k:getattr(r,k)&0xffffffff for k in ['rax','rbx','rcx','rdx','rsi','rdi','rbp','rsp']},stack=[m.u32(r.rsp+i) for i in range(0,20,4)],**state()))
       if name in FUNCTIONS:
        ret=m.u32(r.rsp);pending.append(dict(return_address=ret,function=name,call_id=entries));enable(ret)
    step();enable(addr)
   emit(dict(kind='end',pending=pending))
  for addr in list(enabled):disable(addr)
  print('TRACE_DONE',label,'entries',entries,'frame',m.i32(FRAME_ADDR)-base,flush=True)
  for o in pair:
   m.write_i32(o+0x98,13);m.write_i32(o+0x228,0)
   if m.i32(o+0x6c)>0:call(o,m.u32(m.u32(o)+0xd0))
 finally:
  for addr in list(enabled):disable(addr)
  # Confirm every temporary code byte was restored, including dynamic return sites.
  restored={hex(addr):m.read(addr,1).hex() for addr in originals}
  assert all(m.read(addr,1)==byte for addr,byte in originals.items())
  open('runs/kill-timing/'+label+'-restored.json','w').write(json.dumps(restored,indent=2))
  detach()
