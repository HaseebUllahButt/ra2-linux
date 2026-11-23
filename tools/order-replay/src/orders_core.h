/* order-replay core: the same code runs inside the ORIGINAL game.exe 1.006 (orders.dll under Wine)
 * and inside the JOIN native build (orders_join.c, private binary ra2-orders).
 *
 * The wrapper defines, before it includes this file:
 *   G8(a) G16(a) G32(a)      read guest memory (guest address == host address in both builds)
 *   S8(a,v) S32(a,v)         write guest memory
 *   OR_EXIT()                flush and end the process at once
 *
 * Hook points (the same guest instruction in both builds):
 *   seed      0x005158C4  mov eax,[0xa40d40]   the game reads the custom seed once, at scenario start
 *   pre_queue 0x00540643  call 0x623770        the main loop calls Queue_AI (OutList -> DoList, execute)
 *   boundary  0x0054068A  (after mov [0xa40d2c],edx at 0x540684)  the frame counter was just raised
 *
 * The order path: EventClass records (111 bytes) in EventClass::OutList, a QueueClass<EventClass,128>:
 *   Count 0xA32338, Head 0xA3233C, Tail 0xA32340, List 0xA32344 (111 bytes each), Timings 0xA35AC4.
 * The game's own UI writes there (see 0x623405..0x62345E). Queue_AI_Normal (0x6239EC) moves every entry to
 * DoList (0x866270) unchanged and Execute_DoList (0x628880) runs it in this same call when Frame == frame.
 * EventClass: +0 Type, +1 IsExecuted, +2 HouseIndex, +3 Frame (int), +7 data (104 bytes).
 *
 * Env:
 *   ORDERS_OUT          output folder (required)
 *   ORDERS_FILE         orders to inject: lines "<frame> <222 hex chars>" (house byte FE = the player)
 *   ORDERS_SEED         custom random seed (hex), written at the seed hook
 *   ORDERS_MAX_FRAME    end the process at this frame boundary (default 0 = never)
 *   ORDERS_CENSUS_EVERY census interval in frames (default 30)
 *   ORDERS_SPEED        game speed value held at 0xA40B18 (default 0 = no frame wait; it is a game input:
 *                       compare only runs made with the same value)
 *   ORDERS_FLUSH        1 (default) = flush frames.txt every frame (stall detection, live UI sessions)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define OR_FRAME      0xA40D2Cu
#define OR_CSEED      0xA40D40u
#define OR_SEED       0xA40D3Cu
#define OR_SPEED      0xA40B18u
#define OR_FPS        0xA6EB1Cu
#define OR_FPS_LOW    0xA6EB28u
#define OR_SCENARIO   0xA3D290u
#define OR_PLAYER     0xA35DB4u
#define OL_COUNT      0xA32338u
#define OL_HEAD       0xA3233Cu
#define OL_TAIL       0xA32340u
#define OL_LIST       0xA32344u
#define OL_TIMINGS    0xA35AC4u
#define EV_SIZE       111u
#define OL_SIZE       128u

/* DynamicVectorClass: +4 Items, +0x10 Count */
#define DVC_HOUSES    0xA32298u
#define DVC_INFANTRY  0xA35E48u
#define DVC_UNITS     0x866198u
#define DVC_AIRCRAFT  0xA40398u
#define DVC_BUILDINGS 0xA40AF8u
#define DVC_UNITTYPE  0xA35D48u
#define DVC_BLDGTYPE  0xA35CD8u
#define DVC_INFTYPE   0xA40350u
#define DVC_AIRTYPE   0xA3D278u

typedef struct { uint32_t frame; uint8_t ev[EV_SIZE]; } OrRec;

static int or_ready;
static char or_dir[1024];
static FILE *or_frames, *or_census, *or_events, *or_log;
static OrRec *or_recs; static size_t or_nrec, or_irec;
static uint32_t or_seed, or_max, or_every = 30, or_speed, or_flush = 1;
static uint32_t or_last_boundary = 0xffffffffu;
static unsigned long or_injected, or_logged;

static FILE *or_open(const char *name)
{
    char p[1200]; snprintf(p, sizeof p, "%s/%s", or_dir, name);
    FILE *f = fopen(p, "w");
    if (!f) OR_EXIT();
    return f;
}

static int or_hex(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void or_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(or_log, "cannot read %s\n", path); fflush(or_log); OR_EXIT(); }
    char line[1024]; size_t cap = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        char *e; unsigned long fr = strtoul(p, &e, 10); if (e == p) continue;
        p = e; while (*p == ' ' || *p == '\t') p++;
        OrRec r; memset(&r, 0, sizeof r); r.frame = (uint32_t)fr;
        size_t n = 0;
        while (n < EV_SIZE) { int a = or_hex(p[0]), b = a < 0 ? -1 : or_hex(p[1]); if (a < 0 || b < 0) break; r.ev[n++] = (uint8_t)(a * 16 + b); p += 2; }
        if (n != EV_SIZE) { fprintf(or_log, "bad order line (only %u bytes): %s", (unsigned)n, line); fflush(or_log); OR_EXIT(); }
        if (or_nrec == cap) { cap = cap ? cap * 2 : 256; or_recs = (OrRec *)realloc(or_recs, cap * sizeof *or_recs); }
        or_recs[or_nrec++] = r;
    }
    fclose(f);
    /* stable sort by frame (insertion sort keeps file order inside a frame) */
    for (size_t i = 1; i < or_nrec; i++) { OrRec t = or_recs[i]; size_t j = i; while (j && or_recs[j - 1].frame > t.frame) { or_recs[j] = or_recs[j - 1]; j--; } or_recs[j] = t; }
    fprintf(or_log, "loaded %u orders from %s\n", (unsigned)or_nrec, path); fflush(or_log);
}

static void or_init(void)
{
    if (or_ready) return;
    or_ready = -1;
    const char *d = getenv("ORDERS_OUT"); if (!d || !*d) return;
    snprintf(or_dir, sizeof or_dir, "%s", d);
    or_log = or_open("orders.log");
    or_frames = or_open("frames.txt");
    or_census = or_open("census.txt");
    or_events = or_open("events.txt");
    const char *s;
    if ((s = getenv("ORDERS_SEED")) && *s) or_seed = (uint32_t)strtoul(s, 0, 16);
    if ((s = getenv("ORDERS_MAX_FRAME")) && *s) or_max = (uint32_t)strtoul(s, 0, 10);
    if ((s = getenv("ORDERS_CENSUS_EVERY")) && *s) or_every = (uint32_t)strtoul(s, 0, 10);
    if ((s = getenv("ORDERS_SPEED")) && *s) or_speed = (uint32_t)strtoul(s, 0, 10);
    if ((s = getenv("ORDERS_FLUSH")) && *s) or_flush = (uint32_t)strtoul(s, 0, 10);
    if ((s = getenv("ORDERS_FILE")) && *s) or_load(s);
    fprintf(or_log, "init: seed %08x max %u census every %u speed %u orders %u\n", or_seed, or_max, or_every, or_speed, (unsigned)or_nrec);
    fflush(or_log);
    or_ready = 1;
}

/* the fixed timing inputs: game speed (default 0, no frame wait), measured fps 20, low-fps flag off */
static void or_timing(void)
{
    S32(OR_SPEED, or_speed); S32(OR_FPS, 20); S8(OR_FPS_LOW, 0);
}

static void core_seed(void)
{
    or_init(); if (or_ready != 1) return;
    uint32_t before = G32(OR_CSEED);
    if (or_seed) S32(OR_CSEED, or_seed);
    or_timing();
    fprintf(or_log, "seed hook: frame %u custom seed %08x -> %08x\n", G32(OR_FRAME), before, G32(OR_CSEED)); fflush(or_log);
}

/* symbolic target (5 bytes): id = kind<<24 | nth<<16 | type index, rtti byte 0xFD.
 * kind 1 building, 2 unit, 3 infantry, 4 aircraft. It becomes the nth (0 = first) live object of that type
 * owned by house `house`, in the game's own object list order, as a normal object target {unique id, 0x34}. */
static int or_resolve(uint32_t at, uint32_t house)
{
    if (G8(at + 4) != 0xFD) return 1;
    uint32_t id = G32(at), kind = id >> 24, nth = (id >> 16) & 0xff, ti = id & 0xffff;
    static const uint32_t objs[5] = { 0, DVC_BUILDINGS, DVC_UNITS, DVC_INFANTRY, DVC_AIRCRAFT };
    static const uint32_t types[5] = { 0, DVC_BLDGTYPE, DVC_UNITTYPE, DVC_INFTYPE, DVC_AIRTYPE };
    static const uint32_t toff[5] = { 0, 0x418, 0x5ac, 0x5a8, 0x5ac };   /* vtable slot 33 (type) of each class */
    if (kind < 1 || kind > 4 || ti >= G32(types[kind] + 0x10)) return 0;
    uint32_t tp = G32(G32(types[kind] + 4) + 4 * ti), it = G32(objs[kind] + 4), n = G32(objs[kind] + 0x10);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = G32(it + 4 * i), own = G32(o + 0x1b4);
        if (G32(o + toff[kind]) != tp || !own || G32(own + 0x30) != house || !G8(o + 0x7d) || (int32_t)G32(o + 0x6c) <= 0) continue;
        if (nth--) continue;
        S32(at, G32(o + 0x10)); S8(at + 4, 0x34);
        return 1;
    }
    return 0;
}

/* target fields per event type (offsets in the record) */
static int or_resolve_event(uint32_t at)
{
    uint32_t house = G8(at + 2); uint8_t t = G8(at);
    static const uint8_t mega[] = { 7, 0xe, 0x13, 0x18, 0 }, one[] = { 7, 0 }, arch[] = { 7, 0xc, 0 };
    const uint8_t *o = (t == 4 || t == 5) ? mega : (t == 6 || t == 7 || t == 9 || t == 17 || t == 21 || t == 22) ? one : t == 30 ? arch : 0;
    for (; o && *o; o++) if (!or_resolve(at + *o, house)) return 0;
    return 1;
}

static uint32_t or_player(void)
{
    uint32_t p = G32(OR_PLAYER); return p ? G32(p + 0x30) : 0;
}

static void core_pre_queue(void)
{
    or_init(); if (or_ready != 1) return;
    uint32_t f = G32(OR_FRAME);
    or_timing();
    while (or_irec < or_nrec && or_recs[or_irec].frame < f) {
        fprintf(or_log, "MISSED order for frame %u at frame %u\n", or_recs[or_irec].frame, f); fflush(or_log); or_irec++;
    }
    while (or_irec < or_nrec && or_recs[or_irec].frame == f) {
        OrRec *r = &or_recs[or_irec++];
        uint32_t cnt = G32(OL_COUNT), tail = G32(OL_TAIL);
        if (cnt >= OL_SIZE) { fprintf(or_log, "OutList full at frame %u\n", f); fflush(or_log); continue; }
        uint32_t at = OL_LIST + tail * EV_SIZE;
        for (uint32_t i = 0; i < EV_SIZE; i++) S8(at + i, r->ev[i]);
        S8(at + 1, 0);                                        /* IsExecuted */
        if (r->ev[2] == 0xFE) S8(at + 2, (uint8_t)or_player());   /* the player's house */
        S32(at + 3, f);                                       /* Frame: as the event constructors stamp it */
        if (!or_resolve_event(at)) { fprintf(or_log, "UNRESOLVED order %u at frame %u (no such object now); dropped\n", (unsigned)or_irec, f); fflush(or_log); continue; }
        S32(OL_TIMINGS + tail * 4, 0);                        /* network response timing only; fixed */
        S32(OL_TAIL, (tail + 1) & (OL_SIZE - 1));
        S32(OL_COUNT, cnt + 1);
        or_injected++;
    }
    /* recorder: everything the game will queue now (UI orders and injected orders alike) */
    uint32_t cnt = G32(OL_COUNT), head = G32(OL_HEAD);
    for (uint32_t i = 0; i < cnt && i < OL_SIZE; i++) {
        uint32_t at = OL_LIST + ((head + i) & (OL_SIZE - 1)) * EV_SIZE;
        char hx[EV_SIZE * 2 + 1];
        /* only the bytes the game itself sends for this type over the network (length table 0x7D659C):
           the rest of a record, and IsExecuted, can hold stale stack bytes from the constructor's caller */
        uint8_t t = G8(at); uint32_t used = 7 + (t < 46 ? G8(0x7D659Cu + t) : EV_SIZE - 7);
        for (uint32_t j = 0; j < EV_SIZE; j++) { static const char x[] = "0123456789abcdef"; uint8_t v = (j == 1 || j >= used) ? 0 : G8(at + j); hx[2 * j] = x[v >> 4]; hx[2 * j + 1] = x[v & 15]; }
        hx[EV_SIZE * 2] = 0;
        fprintf(or_events, "%u %s\n", f, hx); or_logged++;
    }
    if (cnt) fflush(or_events);
}

static void or_hash(uint32_t a, uint32_t n, uint32_t *h1, uint32_t *h2)
{
    uint32_t x = 2166136261u, y = 0x9747b28cu;
    for (uint32_t i = 0; i < n; i++) { uint8_t v = G8(a + i); x = (x ^ v) * 16777619u; y = (y ^ v) * 0x01000193u + (y >> 15) + i; }
    *h1 = x; *h2 = y;
}

static void or_id(uint32_t type, char *out)
{
    out[0] = 0; if (!type) { strcpy(out, "-"); return; }
    for (int i = 0; i < 24; i++) { char c = (char)G8(type + 0x24 + i); out[i] = c; if (!c) break; if (c <= ' ' || c > '~') { out[i] = '?'; } }
    out[24] = 0; if (!out[0]) strcpy(out, "-");
}

static void or_types(void)
{
    FILE *f = or_open("types.txt");
    static const struct { uint32_t dvc; int rtti; } T[] = { { DVC_BLDGTYPE, 7 }, { DVC_UNITTYPE, 40 }, { DVC_INFTYPE, 16 }, { DVC_AIRTYPE, 3 } };
    for (int k = 0; k < 4; k++) {
        uint32_t items = G32(T[k].dvc + 4), n = G32(T[k].dvc + 0x10);
        for (uint32_t i = 0; i < n; i++) { char id[32]; or_id(G32(items + 4 * i), id); fprintf(f, "%d %u %s\n", T[k].rtti, i, id); }
    }
    fclose(f);
}

static void or_census_write(uint32_t f)
{
    uint32_t items = G32(DVC_HOUSES + 4), n = G32(DVC_HOUSES + 0x10);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t h = G32(items + 4 * i); char id[32]; or_id(G32(h + 0x34), id);
        fprintf(or_census, "H %u %u %s %d\n", f, G32(h + 0x30), id, (int32_t)G32(h + 0x24c));
        /* super weapons: HouseClass+0x19C DynamicVectorClass<SuperClass*>; SuperClass +0x24 type, +0x2C.. state */
        uint32_t si = G32(h + 0x1a0), sn = G32(h + 0x1ac);
        for (uint32_t k = 0; k < sn && k < 64; k++) {
            uint32_t sp = G32(si + 4 * k); char sid[32];
            or_id(G32(sp + 0x24), sid);
            /* Only fields seen to change on a grant: +0x2C timer start, +0x34 delay, +0x54, +0x5C (the AI Iron
               Curtain grant at frame 19650 in runs/v7-join). Other words in this object are padding or stale
               heap memory before a grant, so they can differ between processes. */
            char hx[64]; snprintf(hx, sizeof hx, " %d %d %d %d", (int32_t)G32(sp + 0x2c), (int32_t)G32(sp + 0x34),
                                  (int32_t)G32(sp + 0x54), (int32_t)G32(sp + 0x5c));
            fprintf(or_census, "S %u %u %u %s%s\n", f, G32(h + 0x30), k, sid, hx);
        }
    }
    static const struct { uint32_t dvc, typeoff; char kind; } K[] = {
        { DVC_BUILDINGS, 0x418, 'B' }, { DVC_UNITS, 0x5ac, 'U' }, { DVC_INFANTRY, 0x5a8, 'I' }, { DVC_AIRCRAFT, 0x5ac, 'A' } };
    for (int k = 0; k < 4; k++) {
        uint32_t it = G32(K[k].dvc + 4), m = G32(K[k].dvc + 0x10);
        for (uint32_t i = 0; i < m; i++) {
            uint32_t o = G32(it + 4 * i); char id[32]; or_id(G32(o + K[k].typeoff), id);
            uint32_t own = G32(o + 0x1b4);
            fprintf(or_census, "%c %u %u %s %d %d %u %d %d %d %d %u\n", K[k].kind, f, G32(o + 0x10), id,
                    own ? (int)G32(own + 0x30) : -1, (int32_t)G32(o + 0x6c), G8(o + 0x7d),
                    (int32_t)G32(o + 0x88), (int32_t)G32(o + 0x8c), (int32_t)G32(o + 0x90), (int32_t)G32(o + 0x98),
                    G16(o + 0x2e0));
        }
    }
    fflush(or_census);
}

/* returns nonzero when the process should end */
static int core_boundary(void)
{
    or_init(); if (or_ready != 1) return 0;
    uint32_t f = G32(OR_FRAME);
    if (f == or_last_boundary) return 0;
    or_last_boundary = f;
    or_timing();
    uint32_t sc = G32(OR_SCENARIO), h1 = 0, h2 = 0, i1 = 0, i2 = 0;
    if (sc) { or_hash(sc + 0x21c, 1008, &h1, &h2); i1 = G32(sc + 0x21c); i2 = G32(sc + 0x220); }   /* Random2Class: 2 indexes + 250-word table */
    uint32_t s1 = 0, s2 = 0; or_hash(0x83921cu, 1008, &s1, &s2);   /* screen RNG: kept for reference, not a pass rule */
    fprintf(or_frames, "%u %u %u %08x%08x %08x%08x %u\n", f, i1, i2, h1, h2, s1, s2, G32(OR_SEED));
    if (f == 1) { or_types(); fprintf(or_log, "battle: player house %u seed %08x\n", or_player(), G32(OR_SEED)); fflush(or_log); }
    if (f == 1 || (or_every && f % or_every == 0)) or_census_write(f);
    if (or_flush || (f & 255) == 0) fflush(or_frames);
    if (or_max && f >= or_max) {
        fprintf(or_log, "end at frame %u: injected %lu, logged %lu, %u of %u orders used\n", f, or_injected, or_logged, (unsigned)or_irec, (unsigned)or_nrec);
        fflush(NULL);
        return 1;
    }
    return 0;
}
