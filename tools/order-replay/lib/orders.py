"""Order scripts <-> raw EventClass records (game.exe 1.006 layouts, from EventClass::Execute 0x4B65C0).

Raw line (what the DLL/JOIN hook reads and what the recorder writes): "<frame> <222 hex chars>".
House byte 0xFE means "the player's house" (filled in at injection).

Script line: "<frame> <verb> <args...>"   ('#' starts a comment)
  move     UID[,UID...] X Y          MEGAMISSION(4) mission Move(2), destination cell
  attack   UID[,UID...] TUID         MEGAMISSION(4) mission Attack(1), target object
  fireat   UID[,UID...] X Y          MEGAMISSION(4) mission Attack(1), cell target (fire at the ground; RA2 has no attack-move)
  guard    UID[,UID...]              MEGAMISSION(4) mission Guard(5)
  mission  UID[,UID...] M T D        MEGAMISSION(4) any mission number, T and D targets (see TARGET)
  stop     UID[,UID...]              IDLE(6)
  deploy   UID                       DEPLOY(9)
  produce  TYPE                      PRODUCE(14)  TYPE is an INI id (GAPOWR, E1, MTNK ...)
  suspend  TYPE / abandon TYPE       SUSPEND(15) / ABANDON(16)
  place    TYPE X Y                  PLACE(11)  the finished building of TYPE's queue (building or defense), cell X Y
  sell     UID                       SELL(22)
  repair   UID                       REPAIR(21)  (toggles repair)
  primary  UID                       PRIMARY(17)
  rally    UID X Y                   ARCHIVE(30)  rally point of a factory at cell X Y
  special  INDEX X Y                 SPECIAL_PLACE(18)  fire the house's super weapon INDEX at cell X Y
  raw      HEX(222)                  one raw record, house byte included
UID: a unique id (1032239), or "@TYPE" / "@TYPE.n": the first / n-th (0-based) live object of that INI type
owned by the player at the moment of injection (resolved by the hook; the logged order has the real id).
TARGET: "-" none, "uN" object with unique id N, "@TYPE.n" as above, "cX,Y" cell.
"""
import struct

EVENT_NAMES = ('EMPTY POWERON POWEROFF ALLY MEGAMISSION MEGAMISSION_F IDLE SCATTER DESTRUCT DEPLOY DETONATE PLACE '
               'OPTIONS GAMESPEED PRODUCE SUSPEND ABANDON PRIMARY SPECIAL_PLACE EXIT ANIMATION REPAIR SELL SELLCELL '
               'SPECIAL FRAMESYNC MESSAGE RESPONSE_TIME FRAMEINFO SAVEGAME ARCHIVE ADDPLAYER TIMING PROCESS_TIME '
               'PAGEUSER REMOVEPLAYER LATENCYFUDGE MEGAFRAMEINFO PACKETTIMING ABOUTTOEXIT FALLBACKHOST ADDRESSCHANGE '
               'PLANCONNECT PLANCOMMIT PLANNODEDELETE ALLCHEER').split()
EV = {n: i for i, n in enumerate(EVENT_NAMES)}
RTTI_ABSTRACT, RTTI_CELL = 0x34, 0x0B
RTTI_KIND = {7: 'building', 40: 'unit', 16: 'infantry', 3: 'aircraft'}
PLAYER = 0xFE
SIZE = 111


def tgt_obj(uid):
    return struct.pack('<IB', uid, RTTI_ABSTRACT)


def tgt_cell(x, y):
    return struct.pack('<IB', y * 1000 + x, RTTI_CELL)


def tgt_none():
    return b'\0' * 5


KIND = {7: 1, 40: 2, 16: 3, 3: 4}
TYPES = {}


def obj(s):
    if s.startswith('@'):
        name, _, n = s[1:].partition('.')
        r, i = TYPES[name.upper()]
        return struct.pack('<IB', KIND[r] << 24 | int(n or 0) << 16 | i, 0xFD)
    return tgt_obj(int(s))


def tgt_parse(s):
    if s.startswith('@'):
        return obj(s)
    if s == '-':
        return tgt_none()
    if s[0] == 'u':
        return tgt_obj(int(s[1:]))
    if s[0] == 'c':
        x, y = s[1:].split(',')
        return tgt_cell(int(x), int(y))
    raise ValueError('bad target ' + s)


def tgt_text(b):
    i, r = struct.unpack('<IB', b)
    if r == 0 and i == 0:
        return '-'
    if r == RTTI_ABSTRACT:
        return 'u%d' % i
    if r == RTTI_CELL:
        return 'c%d,%d' % (i % 1000, i // 1000)
    if r == 0xFD:
        return '@kind%d#%d.%d' % (i >> 24, i & 0xffff, (i >> 16) & 0xff)
    return 'r%d:%d' % (r, i)


def event(etype, payload, house=PLAYER):
    e = bytearray(SIZE)
    e[0] = etype; e[2] = house
    e[7:7 + len(payload)] = payload
    return bytes(e)


def megamission(whom, mission, target=None, dest=None):
    p = whom + bytes([mission]) + b'\0' + (target or tgt_none()) + (dest or tgt_none()) + tgt_none() + b'\0'
    return event(EV['MEGAMISSION'], p)


def load_types(path):
    """types.txt from a run: 'rtti index ID'."""
    t = {}
    for line in open(path):
        r, i, n = line.split()
        t.setdefault(n.upper(), (int(r), int(i)))
    return t


def compile_line(words, types):
    v = words[0].lower(); a = words[1:]
    TYPES.clear(); TYPES.update(types)
    uids = lambda s: [obj(x) for x in s.split(',')]
    if v == 'move':
        return [megamission(u, 2, None, tgt_cell(int(a[1]), int(a[2]))) for u in uids(a[0])]
    if v == 'attack':
        return [megamission(u, 1, obj(a[1]), None) for u in uids(a[0])]
    if v == 'fireat':
        return [megamission(u, 1, tgt_cell(int(a[1]), int(a[2])), None) for u in uids(a[0])]
    if v == 'guard':
        return [megamission(u, 5) for u in uids(a[0])]
    if v == 'mission':
        return [megamission(u, int(a[1]), tgt_parse(a[2]), tgt_parse(a[3])) for u in uids(a[0])]
    if v == 'stop':
        return [event(EV['IDLE'], u) for u in uids(a[0])]
    if v in ('deploy', 'sell', 'repair', 'primary'):
        return [event(EV[{'deploy': 'DEPLOY', 'sell': 'SELL', 'repair': 'REPAIR', 'primary': 'PRIMARY'}[v]],
                      obj(a[0]))]
    if v in ('produce', 'suspend', 'abandon'):
        r, i = types[a[0].upper()]
        return [event(EV[v.upper()], struct.pack('<iii', r, i, 0))]
    if v == 'place':
        r, i = types[a[0].upper()]
        # the real type index: House::Place_Object (0x4E6950) takes the type's BuildCat (+0xBA0) to choose the
        # building queue (+0x52E8) or the defense queue (BuildCat 5, +0x52F8); with -1 it uses the building queue
        return [event(EV['PLACE'], struct.pack('<iii', r, i, 0) + struct.pack('<hh', int(a[1]), int(a[2])))]
    if v == 'rally':
        return [event(EV['ARCHIVE'], obj(a[0]) + tgt_cell(int(a[1]), int(a[2])))]
    if v == 'special':
        return [event(EV['SPECIAL_PLACE'], struct.pack('<ihh', int(a[0]), int(a[1]), int(a[2])))]
    if v == 'raw':
        b = bytes.fromhex(a[0]); assert len(b) == SIZE
        return [b]
    raise ValueError('unknown verb ' + v)


def compile_script(lines, types):
    out = []
    for n, line in enumerate(lines, 1):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        w = line.split()
        try:
            for e in compile_line(w[1:], types):
                out.append((int(w[0]), e))
        except Exception as x:
            raise SystemExit('line %d: %s: %s' % (n, line, x))
    return out


def decode(e, names=None):
    """One raw record -> script-like text (names: {(rtti, index): ID})."""
    t = e[0]; house = e[2]; frame = struct.unpack_from('<i', e, 3)[0]; d = e[7:]
    n = EVENT_NAMES[t] if t < len(EVENT_NAMES) else 'T%d' % t
    nm = lambda r, i: (names or {}).get((r, i), '%s#%d' % (RTTI_KIND.get(r, 'rtti%d' % r), i))
    if t in (EV['MEGAMISSION'], EV['MEGAMISSION_F']):
        s = 'mission %s %d %s %s follow=%s planning=%d' % (tgt_text(d[0:5]), d[5], tgt_text(d[7:12]), tgt_text(d[12:17]),
                                                         tgt_text(d[17:22]), d[22])
    elif t in (EV['DEPLOY'], EV['SELL'], EV['REPAIR'], EV['PRIMARY'], EV['IDLE'], EV['SCATTER']):
        s = tgt_text(d[0:5])
    elif t in (EV['PRODUCE'], EV['SUSPEND'], EV['ABANDON']):
        r, i, nav = struct.unpack_from('<iii', d)
        s = '%s naval=%d' % (nm(r, i), nav)
    elif t == EV['PLACE']:
        r, i, nav = struct.unpack_from('<iii', d); x, y = struct.unpack_from('<hh', d, 12)
        s = 'rtti=%d id=%d naval=%d cell %d,%d' % (r, i, nav, x, y)
    elif t == EV['ARCHIVE']:
        s = '%s -> %s' % (tgt_text(d[0:5]), tgt_text(d[5:10]))
    elif t == EV['SPECIAL_PLACE']:
        i, x, y = struct.unpack_from('<ihh', d)
        s = 'super %d cell %d,%d' % (i, x, y)
    else:
        s = d.rstrip(b'\0').hex() or '-'
    return 'house=%d frame=%d %s %s' % (house, frame, n, s)


def read_raw(path):
    out = []
    for line in open(path):
        line = line.split('#', 1)[0].strip()
        if line:
            f, h = line.split()
            out.append((int(f), bytes.fromhex(h)))
    return out


def write_raw(path, recs, header=''):
    with open(path, 'w') as f:
        if header:
            f.write(''.join('# ' + l + '\n' for l in header.splitlines()))
        for fr, e in recs:
            f.write('%d %s\n' % (fr, e.hex()))
