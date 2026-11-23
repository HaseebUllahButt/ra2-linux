#!/usr/bin/env python3
"""Census of the ORIGINAL game: start game.exe as our own child (so Linux lets us
read its memory), then every --every seconds print one CENSUS line per house:
money, live buildings, vehicles, infantry, aircraft, miners, new items, and a
type list. --immortal-house COUNTRY keeps that house's buildings at full health.

Layout from the typed export (game.exe 1.006):
  vtables: House 007a2de0, Building 0079cbbc, Unit 007addf8, Infantry 007a3540, Aircraft 0079b12c
  Object: Health +0x6c, IsAlive +0x7d; Techno: Owner +0x1b4; Building: Type +0x418; Unit: Type +0x5ac
  House: ArrayIndex +0x30, Type +0x34, Balance +0x24c; AbstractType: ID char[25] at +0x24 (checked at run time)
  Game frame counter: int at 0x00a40d2c. The main loop FUN_0053fbd0 adds 1 each game frame
  (not when paused); frame timers subtract their start from it (export: 73 "DAT_00a40d2c - ..." reads).
  CENSUS lines print it as frame=, so runs at any game speed compare by game time.
--fast: remove the frame wait (Game Speed 0 in memory, see bin/fast_mode.py) once FILE <out>/fast.go exists
  (or at battle start with --fast-now). Then the clock is the game frame: sec = frames since battle / 20.833
  (Game Speed 3 rate), samples stop the game (SIGSTOP) so one sample = one frame, and hit points are restored
  every ~2 ms. --fast also holds the game's measured frame rate at 20 (--pin-fps), because the original
  game skips some effects (and so changes the match) when it measures under 15 fps. Game rules are not changed;
  --seed N fixes the random seed (same match every time).
"""
import argparse, os, re, signal, struct, subprocess, sys, time

VT = {0x7a2de0: 'house', 0x79cbbc: 'building', 0x7addf8: 'unit', 0x7a3540: 'infantry', 0x79b12c: 'aircraft'}
TYPE_OFF = {'building': 0x418, 'unit': 0x5ac}
MINERS = {'HARV', 'CMIN'}
FRAME_ADDR = 0xa40d2c
ID_RE = re.compile(rb'[A-Za-z0-9_]{2,24}\x00')

ap = argparse.ArgumentParser()
ap.add_argument('--display', type=int, required=True)
ap.add_argument('--poll-ms', type=float, default=20, help='frame counter sampling interval in milliseconds')
ap.add_argument('--oracle', help='directory containing the game, Wine prefix and reference capture tools')
ap.add_argument('--out', required=True)
ap.add_argument('--minutes', type=float, default=33)
ap.add_argument('--every', type=float, default=30)
ap.add_argument('--immortal-house', default='')
ap.add_argument('--fast', action='store_true', help='fast mode after <out>/fast.go appears')
ap.add_argument('--fast-now', action='store_true', help='fast mode from battle start')
ap.add_argument('--pin-fps', type=int, default=-1, help='hold the measured frame rate (default: 20 with --fast, else off)')
ap.add_argument('--seed', default='0', help='fixed random seed (0 = game picks one)')
ap.add_argument('--attach-only', type=int, default=0, help='pid of a game.exe we already started (test only)')
ap.add_argument('--every-frames', type=int, default=0, help='sample every N game frames (stop the game with SIGSTOP at the sample)')
ap.add_argument('--dense', default='', help='F:G  sample every frame from F to G (absolute frame numbers)')
ap.add_argument('--max-frames', type=int, default=0, help='stop after this many frames since battle start')
ap.add_argument('--native-cmd', default='', help='shell command that starts a native (Linux) game as our child; the process named ra2* is read')
ap.add_argument('--native-cwd', default='.')
ap.add_argument('--native-comm', default='ra2')
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)
log = open(os.path.join(a.out, 'census.log'), 'a', buffering=1)
def say(*x):
    s = ' '.join(str(i) for i in x); print(s, flush=True); log.write(s + '\n')

children = []
# Become a child subreaper: Wine's start.exe exits after it launches game.exe,
# and the orphaned game must stay our descendant for Yama ptrace_scope=1.
import ctypes
ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0)
def start():
    if a.native_cmd:
        env = dict(os.environ, RA2_TARGET=os.environ.get('RA2_TARGET', 'game'), SDL_AUDIODRIVER='dummy', PULSE_SERVER='unix:/nonexistent', PIPEWIRE_REMOTE='/nonexistent')
        w = subprocess.Popen(a.native_cmd, shell=True, cwd=a.native_cwd, env=env, stdout=open(os.path.join(a.out, 'native.log'), 'w'), stderr=subprocess.STDOUT, start_new_session=True)
        children.append(w); return None
    if not a.oracle:
        ap.error('--oracle is required for a Wine reference run')
    n = a.display; d = os.path.join(a.oracle, 'instances', str(n))
    if not os.path.isdir(d):
        os.makedirs(d); subprocess.run(['cp', '-a', '--reflink=auto', a.oracle + '/prefix', d + '/prefix'], check=True)
        subprocess.run(['cp', '-a', a.oracle + '/game', d + '/game'], check=True)
    if os.path.exists(f'/tmp/.X11-unix/X{n}'): sys.exit(f'display :{n} is in use')
    tmp = f'/tmp/ra2-oracle-{n}'; os.makedirs(tmp, exist_ok=True)
    x = subprocess.Popen(['Xvfb', f':{n}', '-screen', '0', '1024x768x24', '-nolisten', 'tcp'],
                         stdout=open(tmp + '/xvfb.log', 'w'), stderr=subprocess.STDOUT)
    children.append(x); open(tmp + '/xvfb.pid', 'w').write(str(x.pid))
    for _ in range(50):
        if os.path.exists(f'/tmp/.X11-unix/X{n}'): break
        time.sleep(0.1)
    env = dict(os.environ, WINEDEBUG='-all', WAYLAND_DISPLAY='', WINEPREFIX=d + '/prefix', DISPLAY=f':{n}',
               WINEDLLOVERRIDES='ddraw=n,b;winemenubuilder.exe=d;winepulse.drv=d;winealsa.drv=d;wineoss.drv=d;winejack.drv=d;winecoreaudio.drv=d')
    env.update(PULSE_SERVER='unix:/nonexistent', PIPEWIRE_REMOTE='/nonexistent', ALSA_CARD='none', SDL_AUDIODRIVER='dummy')
    subprocess.run(['wine', 'reg', 'add', 'HKCU\\Software\\Wine\\Drivers', '/v', 'Audio', '/t', 'REG_SZ', '/d', '', '/f'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # TRACE: keep one wineserver and the helper processes running WITHOUT relay, so only game.exe is traced
    subprocess.run(['wineserver', '-p'], env=env)
    subprocess.run(['wine', 'wineboot', '-u'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180)
    time.sleep(3)
    od = os.path.abspath(a.out)
    for f in ('rclaunch.exe', 'rec.dll'):
        subprocess.run(['cp', '-f', os.path.join(a.oracle, 'recomp-trace/win', f), d + '/game/' + f], check=True)
    env.update(REC_MODE=os.environ.get('REC_MODE', 'record'), REC_FILE=os.environ.get('REC_FILE') or od + '/rec.bin',
               REC_HASH=od + '/hash.bin', REC_DUMP=os.environ.get('REC_DUMP') or '')
    if os.environ.get('REC_MASK'): env['REC_MASK'] = os.environ['REC_MASK']
    env.pop('REC_ALLOC', None)
    if os.environ.get('REC_CHUNK'): env['REC_CHUNK'] = os.environ['REC_CHUNK']
    if os.environ.get('REC_ALLOC') == '1': env['REC_ALLOC'] = '1'
    w = subprocess.Popen(['wine', 'rclaunch.exe', 'game.exe', 'rec.dll'], cwd=d + '/game', env=env,
                         stdout=open(tmp + '/game.log', 'w'), stderr=subprocess.STDOUT)
    children.append(w); open(tmp + '/wine.pid', 'w').write(str(w.pid))
    return d + '/prefix'

def descendants(root):
    kids = {}
    for p in os.listdir('/proc'):
        if p.isdigit():
            try: kids.setdefault(int(open(f'/proc/{p}/stat').read().rsplit(')', 1)[1].split()[1]), []).append(int(p))
            except Exception: pass
    out, todo = [], [root]
    while todo:
        q = todo.pop(); out.append(q); todo += kids.get(q, [])
    return out

def find_game(prefix):
    for _ in range(1200 if a.native_cmd else 240):
        for p in descendants(os.getpid()):
            if a.native_cmd:
                try:
                    if open(f'/proc/{p}/comm').read().strip().startswith(a.native_comm) and p != os.getpid(): return p
                except Exception: pass
                continue
            try:
                cmd = open(f'/proc/{p}/cmdline', 'rb').read()
                env = open(f'/proc/{p}/environ', 'rb').read().split(b'\0')
            except Exception: continue
            argv0 = cmd.split(b'\0')[0].replace(b'\\', b'/').rsplit(b'/', 1)[-1].lower()
            if argv0 == b'game.exe' and ('WINEPREFIX=' + prefix).encode() in env: return p
        time.sleep(0.5)
    sys.exit('game.exe did not start')

class Mem:
    def __init__(s, pid): s.pid = pid; s.f = open(f'/proc/{pid}/mem', 'r+b', buffering=0)
    def read(s, addr, n):
        try: s.f.seek(addr); return s.f.read(n)
        except OSError: return b''
    def u32(s, addr):
        b = s.read(addr, 4); return struct.unpack('<I', b)[0] if len(b) == 4 else 0
    def i32(s, addr):
        b = s.read(addr, 4); return struct.unpack('<i', b)[0] if len(b) == 4 else 0
    def write_i32(s, addr, v):
        try: s.f.seek(addr); s.f.write(struct.pack('<i', v))
        except OSError: pass
    def regions(s):
        for line in open(f'/proc/{s.pid}/maps'):
            parts = line.split()
            lo, hi = (int(x, 16) for x in parts[0].split('-'))
            if parts[1].startswith('rw') and hi <= 0x80000000 and hi - lo <= 512 << 20 and (len(parts) < 6 or parts[5] == '[heap]'):
                yield lo, hi

PAT = re.compile(b'|'.join(re.escape(struct.pack('<I', v)) for v in VT))
def scan(m):
    found = {k: [] for k in VT.values()}
    for lo, hi in m.regions():
        for base in range(lo, hi, 64 << 20):
            buf = m.read(base, min(64 << 20, hi - base))
            for mt in PAT.finditer(buf):
                if mt.start() % 4: continue
                found[VT[struct.unpack('<I', mt.group())[0]]].append(base + mt.start())
    return found

idcache = {}
def type_id(m, ptr):
    if ptr in idcache: return idcache[ptr]
    b = m.read(ptr + 0x24, 25); mt = ID_RE.match(b)
    v = mt.group()[:-1].decode() if mt else None
    if v: idcache[ptr] = v
    return v

def discover_type_offset(m, objs):
    """Infantry and aircraft: find the Type pointer offset whose target has a valid ID."""
    best = {}
    for o in objs[:40]:
        blob = m.read(o, 0x800)
        for off in range(0x400, len(blob) - 4, 4):
            p = struct.unpack_from('<I', blob, off)[0]
            if 0x10000 < p < 0x80000000 and m.u32(p) not in VT and type_id(m, p):
                best[off] = best.get(off, 0) + 1
    return max(best, key=best.get) if best else None

def main():
    if a.attach_only: pid = a.attach_only
    else: prefix = start(); pid = find_game(prefix)
    time.sleep(5)
    try: leak = subprocess.run('pw-cli ls Node | grep -c "Red Alert"', shell=True, capture_output=True, text=True).stdout.strip()
    except Exception: leak = '0'
    say(f'# sound leak check: {leak}')
    if leak not in ('0', ''):
        os.kill(pid, signal.SIGKILL); sys.exit('SOUND LEAK: game killed')
    say(f'# census pid={pid} display=:{a.display} immortal={a.immortal_house or "-"}')
    m = Mem(pid)
    t0 = None; seen = {}; immortal = {}; next_sample = 0; restored = [0]
    byf = a.every_frames > 0; fast = a.fast or a.fast_now; fast_on = False; f0 = None; next_frame = 0; t_real = time.time()
    pin = a.pin_fps if a.pin_fps >= 0 else (20 if fast else 0); pinned = False
    if fast or pin or int(a.seed, 0):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import fast_mode as FM
    seed = int(a.seed, 0)
    while True:
        if not os.path.exists(f'/proc/{pid}'): say('# game.exe ended'); break
        now = time.time()
        if pin and not pinned and m.read(FM.FPS_STORE, 5) in (FM.FPS_STORE_BYTES, b'\x90' * 5):
            pinned = FM.pin_fps(m, pin); say(f'# frame rate held at {pin}: {pinned}')
        if seed and t0 is None and m.i32(FM.CUSTOM_SEED) != seed:   # keep it set until the battle starts
            m.write_i32(FM.CUSTOM_SEED, seed)
            if m.i32(FM.CUSTOM_SEED) == seed: say(f'# fixed seed {seed:#x} written')
        if fast and t0 is not None and not fast_on and (a.fast_now or os.path.exists(os.path.join(a.out, 'fast.go'))):
            FM.set_fast(m, True); fast_on = True
            say(f'# fast mode on at frame={m.i32(FRAME_ADDR)} speed={m.i32(FM.GAME_SPEED)}')
        if (byf or fast) and t0 is not None:
            due = m.i32(FRAME_ADDR) >= next_frame
        else: due = now >= next_sample
        if due:
            if (fast or byf) and t0 is not None: os.kill(pid, signal.SIGSTOP)
            try: f = scan(m)
            except Exception:
                if (fast or byf) and t0 is not None: os.kill(pid, signal.SIGCONT)
                raise
            houses = {}
            for h in f['house']:
                tp = m.u32(h + 0x34); cid = type_id(m, tp) if tp else None
                idx = m.i32(h + 0x30)
                if cid and 0 <= idx < 16: houses[h] = (idx, cid, m.i32(h + 0x24c))
            if not houses or not (f['building'] or f['unit']):
                if (fast or byf) and t0 is not None: os.kill(pid, signal.SIGCONT)
                next_sample = now + 2; continue
            if t0 is None:
                t0 = now; f0 = m.i32(FRAME_ADDR); say(f'# battle seen at {time.strftime("%H:%M:%S")} frame={f0}')
                if fast or seed: say(f'# seed used {m.u32(FM.SEED):#x}')
            for k in ('infantry', 'aircraft'):
                if f[k] and k not in TYPE_OFF:
                    off = discover_type_offset(m, f[k])
                    if off: TYPE_OFF[k] = off; say(f'# {k} Type offset 0x{off:x}')
            frame = m.i32(FRAME_ADDR)
            el = (frame - f0) / 20.833 if (fast or byf) else now - t0
            stats = {h: dict(building=0, unit=0, infantry=0, aircraft=0, miners=0, new_b=0, new_u=0, types={}) for h in houses}
            for kind in ('building', 'unit', 'infantry', 'aircraft'):
                for o in f[kind]:
                    blob = m.read(o, 0x1b8)
                    if len(blob) < 0x1b8 or blob[0x7d] != 1: continue
                    hp = struct.unpack_from('<i', blob, 0x6c)[0]; owner = struct.unpack_from('<I', blob, 0x1b4)[0]
                    if hp <= 0 or owner not in houses: continue
                    tid = type_id(m, m.u32(o + TYPE_OFF[kind])) if kind in TYPE_OFF else '?'
                    s = stats[owner]; s[kind] += 1; s['types'][tid] = s['types'].get(tid, 0) + 1
                    if tid in MINERS: s['miners'] += 1
                    uid = m.u32(o + 0x10)
                    if (owner, uid) not in seen:
                        seen[(owner, uid)] = el
                        if t0 != now:  # items present at the first sample are start items
                            s['new_b' if kind == 'building' else 'new_u'] += 1
                    if kind == 'building' and a.immortal_house and houses[owner][1] == a.immortal_house:
                        immortal[o + 0x6c] = max(immortal.get(o + 0x6c, 0), hp)
            for h, (idx, cid, bal) in sorted(houses.items(), key=lambda x: x[1][0]):
                s = stats[h]
                types = ','.join(f'{k}:{v}' for k, v in sorted(s['types'].items()))
                say(f'CENSUS sec={el:.0f} frame={frame} house={idx} country={cid} cash={bal} structures={s["building"]} '
                    f'vehicles={s["unit"]} infantry={s["infantry"]} aircraft={s["aircraft"]} miners={s["miners"]} '
                    f'new_structures={s["new_b"]} new_units={s["new_u"]} types={types}')
            next_sample = now + a.every
            if byf:
                os.kill(pid, signal.SIGCONT); next_frame = (frame // a.every_frames + 1) * a.every_frames
                if a.dense and int(a.dense.split(':')[0]) <= frame < int(a.dense.split(':')[1]): next_frame = frame + 1
                if a.max_frames and frame - f0 >= a.max_frames: say('# done'); break
            elif fast:
                os.kill(pid, signal.SIGCONT)
                next_frame = f0 + round((int(el / a.every + 1e-9) + 1) * a.every * FM.FPS_SPEED3)
                say(f'# real {now - t_real:.0f}s frame {frame} fast={"on" if fast_on else "off"}')
            if a.immortal_house: say(f'# immortal watching {len(immortal)} buildings, restores so far {restored[0]}')
            if el >= a.minutes * 60: say('# done'); break
        for addr, hp in list(immortal.items()):
            cur = m.i32(addr)
            if cur > 0:
                if cur < hp: m.write_i32(addr, hp); restored[0] += 1
            else: immortal.pop(addr); say(f'# immortal: building at {addr:x} reads hp {cur}, dropped')
        time.sleep(0.002 if fast_on else a.poll_ms / 1000)

try: main()
finally:
    if not a.attach_only and os.environ.get('CENSUS_KEEP_GAME') != '1':
        for c in children:
            try:
                if a.native_cmd: os.killpg(c.pid, signal.SIGTERM)
                else: c.send_signal(signal.SIGTERM)
            except Exception: pass
