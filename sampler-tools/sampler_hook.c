/* JOIN in-process frame sampler (private test build only).
 *
 * Two hooks are installed into the lifted game sources by install_sampler.py:
 *
 *  1. join_sampler_frame(site)  - the frame boundary.  The lifted game
 *     increments the game frame counter (guest 0x00A40D2C) at 0x00540684 and
 *     the instruction at 0x0054068A (je 0x54069d) follows it.  The hook is
 *     placed on that instruction boundary, so it runs once per game frame,
 *     right after the counter was raised.  It writes one row per frame with
 *     the SHA-256 of the scenario region at *(uint32_t*)0xa3d290 + 0x218.
 *
 *  2. join_sampler_damage(target, attacker, old_health, site) - the health
 *     write of the damage method VoxelAnimClass__virtual_86 (0x005D3D20).
 *     Three write sites are hooked: 0x005D3DC7 (negative damage, the repair
 *     path), 0x005D3E65 (the damage itself) and 0x005D3F11 (the rage
 *     re-health).  The hook records frame, attacker, target, damage and the
 *     target health after the write.
 *
 * The capture reads state and writes evidence files. After the frame-1
 * snapshot it sets internal speed to zero, as the original recorder does.
 * The private generated build also pins the measured-FPS store at 20.
 * These timing controls change no rules or random values directly.
 * The capture flushes all files and exits at the frame-9000 boundary.
 */
#define _GNU_SOURCE
#include "win32hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* SHA-256, 32 bit only (no 64 bit arithmetic, so the host FPU state   */
/* that the lifted game keeps in g_st/g_x87 is never touched).         */
/* ------------------------------------------------------------------ */

static const uint32_t sha_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t sha_ror(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

static void sha256_hex(const uint8_t *data, uint32_t len, char *out)
{
    uint8_t buf[2048];
    uint32_t i, blocks, w[64], a, b, c, d, e, f, g, h, t1, t2, H[8];
    if (len + 72u > (uint32_t)sizeof buf) { out[0] = 0; return; }
    memcpy(buf, data, len);
    buf[len] = 0x80u;
    blocks = (len + 9u + 63u) / 64u;
    memset(buf + len + 1u, 0, (size_t)(blocks * 64u - len - 1u));
    buf[blocks * 64u - 8u] = 0u; buf[blocks * 64u - 7u] = 0u;
    buf[blocks * 64u - 6u] = 0u; buf[blocks * 64u - 5u] = 0u;
    buf[blocks * 64u - 4u] = (uint8_t)(len >> 21);
    buf[blocks * 64u - 3u] = (uint8_t)(len >> 13);
    buf[blocks * 64u - 2u] = (uint8_t)(len >> 5);
    buf[blocks * 64u - 1u] = (uint8_t)((len << 3) & 0xFFu);
    H[0] = 0x6a09e667u; H[1] = 0xbb67ae85u; H[2] = 0x3c6ef372u; H[3] = 0xa54ff53au;
    H[4] = 0x510e527fu; H[5] = 0x9b05688cu; H[6] = 0x1f83d9abu; H[7] = 0x5be0cd19u;
    for (i = 0; i < blocks; i++) {
        const uint8_t *p = buf + i * 64u;
        uint32_t j;
        for (j = 0; j < 16u; j++)
            w[j] = ((uint32_t)p[4u * j] << 24) | ((uint32_t)p[4u * j + 1u] << 16) |
                   ((uint32_t)p[4u * j + 2u] << 8) | (uint32_t)p[4u * j + 3u];
        for (j = 16u; j < 64u; j++) {
            uint32_t s0 = sha_ror(w[j - 15u], 7u) ^ sha_ror(w[j - 15u], 18u) ^ (w[j - 15u] >> 3);
            uint32_t s1 = sha_ror(w[j - 2u], 17u) ^ sha_ror(w[j - 2u], 19u) ^ (w[j - 2u] >> 10);
            w[j] = w[j - 16u] + s0 + w[j - 7u] + s1;
        }
        a = H[0]; b = H[1]; c = H[2]; d = H[3]; e = H[4]; f = H[5]; g = H[6]; h = H[7];
        for (j = 0; j < 64u; j++) {
            uint32_t S1 = sha_ror(e, 6u) ^ sha_ror(e, 11u) ^ sha_ror(e, 25u);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t S0 = sha_ror(a, 2u) ^ sha_ror(a, 13u) ^ sha_ror(a, 22u);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            t1 = h + S1 + ch + sha_k[j] + w[j];
            t2 = S0 + mj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        H[0] += a; H[1] += b; H[2] += c; H[3] += d;
        H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }
    for (i = 0; i < 8u; i++) {
        static const char hex[] = "0123456789abcdef";
        uint32_t v = H[i];
        out[8u * i + 0u] = hex[(v >> 28) & 15u]; out[8u * i + 1u] = hex[(v >> 24) & 15u];
        out[8u * i + 2u] = hex[(v >> 20) & 15u]; out[8u * i + 3u] = hex[(v >> 16) & 15u];
        out[8u * i + 4u] = hex[(v >> 12) & 15u]; out[8u * i + 5u] = hex[(v >> 8) & 15u];
        out[8u * i + 6u] = hex[(v >> 4) & 15u];  out[8u * i + 7u] = hex[v & 15u];
    }
    out[64] = 0;
}

/* ------------------------------------------------------------------ */
/* sampler state                                                       */
/* ------------------------------------------------------------------ */

#define RNG_VA 0x839218u
#define RNG_LEN 0x410u

static FILE *f_frames, *f_damage, *f_regions, *f_objects, *f_health;
static uint32_t gi_addr;
static int enabled = -1;
static uint32_t last_frame;
static int census_every = 30;
static int census_from, census_to, census_out;
static unsigned long long frames_written, damage_written, duplicate_hits, objects_written;

/* ------------------------------------------------------------------ */
/* readable and writable memory ranges (the type objects live on the   */
/* heap, so the image range is not enough)                             */
/* ------------------------------------------------------------------ */

#define MAX_RANGES 512
static uint32_t range_lo[MAX_RANGES], range_hi[MAX_RANGES];
static int range_count;
static uint32_t write_lo[MAX_RANGES], write_hi[MAX_RANGES];
static int write_count;

static void load_ranges(void)
{
    FILE *m = fopen("/proc/self/maps", "r");
    char line[512], perm[8];
    unsigned long lo, hi;
    FILE *dump;
    char path[1024];
    const char *dir = getenv("JOIN_SAMPLER_OUT");
    range_count = 0;
    write_count = 0;
    if (!m) return;
    dump = NULL;
    if (dir) {
        snprintf(path, sizeof path, "%s/maps.txt", dir);
        dump = fopen(path, "w");
    }
    while (fgets(line, sizeof line, m)) {
        if (dump) fputs(line, dump);
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perm) != 3) continue;
        if (perm[0] != 'r') continue;
        if (range_count < MAX_RANGES) {
            range_lo[range_count] = (uint32_t)lo;
            range_hi[range_count] = (uint32_t)hi;
            range_count++;
        }
        if (perm[1] == 'w' && write_count < MAX_RANGES) {
            write_lo[write_count] = (uint32_t)lo;
            write_hi[write_count] = (uint32_t)hi;
            write_count++;
        }
    }
    fclose(m);
    if (dump) fclose(dump);
}

static int readable(uint32_t addr, uint32_t len)
{
    int i;
    for (i = 0; i < range_count; i++)
        if (addr >= range_lo[i] && len <= range_hi[i] - addr) return 1;
    return 0;
}

static void sampler_open(void)
{
    const char *dir = getenv("JOIN_SAMPLER_OUT");
    char path[1024];
    const char *e;
    if (!dir) { enabled = 0; return; }
    snprintf(path, sizeof path, "%s/frames.csv", dir);
    f_frames = fopen(path, "w");
    snprintf(path, sizeof path, "%s/damage.jsonl", dir);
    f_damage = fopen(path, "w");
    snprintf(path, sizeof path, "%s/rng_regions.bin", dir);
    f_regions = fopen(path, "wb");
    snprintf(path, sizeof path, "%s/objects.csv", dir);
    f_objects = fopen(path, "w");
    if (!f_frames || !f_damage || !f_regions || !f_objects) {
        fprintf(stderr, "[sampler] cannot write into %s\n", dir);
        exit(80);
    }
    fprintf(f_frames, "frame,site,sha410,h839218,rng,fixture1012,slot,h_index,h_table,h_tail4,flag,scenario,address,seed,speed,fps\n");
    fflush(f_frames);
    fprintf(f_objects, "frame,kind,addr,uid,type,owner,hp,mission,target,target_uid,stage,x,y,z,facing,firing,prone\n");
    fflush(f_objects);
    snprintf(path, sizeof path, "%s/gi-health.csv", dir);
    f_health = fopen(path, "w");
    if (!f_health) exit(80);
    fprintf(f_health, "frame,uid,hp,alive,addr\n");
    load_ranges();
    e = getenv("JOIN_SAMPLER_CENSUS");
    census_every = e ? atoi(e) : 30;
    if (census_every < 1) census_every = 0;
    e = getenv("JOIN_SAMPLER_CENSUS_FROM");
    census_from = e ? atoi(e) : 0;
    e = getenv("JOIN_SAMPLER_CENSUS_TO");
    census_to = e ? atoi(e) : 0;
    e = getenv("JOIN_SAMPLER_CENSUS_OUT");
    census_out = e ? atoi(e) : 0;
    fprintf(stderr, "[sampler] on: out=%s rng=0x%08x len=0x%x ranges=%d census=%d window=%d..%d out=%d\n",
            dir, RNG_VA, RNG_LEN, range_count, census_every, census_from, census_to, census_out);
}

/* describe a game object: kind, type id, unique id, owner house */
static int id_text_ok(const char *s)
{
    int n = 0;
    while (n < 25 && s[n]) {
        unsigned char c = (unsigned char)s[n];
        if (c < 0x21u || c > 0x7Eu) return 0;
        n++;
    }
    return n >= 1 && n < 25 && s[n] == 0;
}

/* copy the id string of the type object at tp (the id is at tp + 0x24) */
static int type_id_at(uint32_t tp, char *out)
{
    const char *s;
    if (!readable(tp + 0x24u, 25u)) return 0;
    s = (const char *)(uintptr_t)(tp + 0x24);
    if (!id_text_ok(s)) return 0;
    strcpy(out, s);
    return 1;
}

static void obj_info(uint32_t o, char *kind, char *id, char *house, uint32_t *uid, uint32_t *owner,
                     uint32_t *type_ptr)
{
    static const uint32_t type_offs[3] = {0x5A8u, 0x5ACu, 0x418u};
    uint32_t vt, i, hp, tp;
    strcpy(kind, "none"); strcpy(id, "none"); strcpy(house, "none");
    *uid = 0; *owner = 0; *type_ptr = 0;
    if (!o) return;
    vt = MEM32(o);
    if (vt == 0x7a2de0u) { strcpy(kind, "house"); }
    else if (vt == 0x79cbbcu) { strcpy(kind, "building"); }
    else if (vt == 0x7a3540u) { strcpy(kind, "infantry"); }
    else if (vt == 0x7addf8u) { strcpy(kind, "unit"); }
    else if (vt == 0x79b12cu) { strcpy(kind, "aircraft"); }
    else { strcpy(kind, "object"); }
    *uid = MEM32(o + 0x10);
    *owner = MEM32(o + 0x1B4);
    strcpy(id, "?");
    for (i = 0; i < 3u; i++) {
        tp = MEM32(o + type_offs[i]);
        if (!*type_ptr) *type_ptr = tp;
        if (type_id_at(tp, id)) { *type_ptr = tp; break; }
    }
    /* the owner house: the country name is the id of the house type */
    strcpy(house, "?");
    hp = *owner;
    if (readable(hp + 0x34u, 4u)) {
        tp = MEM32(hp + 0x34);
        type_id_at(tp, house);
    }
}

/* ------------------------------------------------------------------ */
/* per object detail, the same fields the original capture records      */
/* ------------------------------------------------------------------ */

struct obj_detail {
    int32_t hp, x, y, z, mission, stage;
    uint32_t target, target_uid;
    uint16_t facing;
    uint8_t alive, firing, prone;
};

static void obj_detail_of(uint32_t o, struct obj_detail *d)
{
    memset(d, 0, sizeof *d);
    if (!o) return;
    d->hp = (int32_t)MEM32(o + 0x6C);
    d->alive = MEM8(o + 0x7D);
    d->x = (int32_t)MEM32(o + 0x88);
    d->y = (int32_t)MEM32(o + 0x8C);
    d->z = (int32_t)MEM32(o + 0x90);
    d->mission = (int32_t)MEM32(o + 0x98);
    d->stage = (int32_t)MEM32(o + 0xE0);
    d->target = MEM32(o + 0x228);
    if (d->target && readable(d->target + 0x10u, 4u)) d->target_uid = MEM32(d->target + 0x10);
    d->facing = MEM16(o + 0x2E0);
    d->firing = MEM8(o + 0x57D);
    d->prone = MEM8(o + 0x5C3);
}

/* ------------------------------------------------------------------ */
/* the object census: every live object, every census_every frames      */
/*                                                                        */
/* The scan reads the memory through /proc/self/mem.  A direct read of  */
/* a region that the host frees while the scan runs would kill the     */
/* game; a read through /proc/self/mem only fails, and the scan then    */
/* skips that part.                                                      */
/* ------------------------------------------------------------------ */

static const char *kind_name(int kind)
{
    switch (kind) {
    case 1: return "building";
    case 2: return "infantry";
    case 3: return "unit";
    case 4: return "aircraft";
    }
    return "?";
}

static int mem_fd = -1;

static uint32_t pread_u32(int fd, uint32_t addr)
{
    uint8_t b[4];
    if (pread(fd, b, 4, (off_t)addr) != 4) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static int pread_id(int fd, uint32_t addr, char *out)
{
    uint8_t b[25];
    int n = 0;
    if (pread(fd, b, 25, (off_t)addr) != 25) return 0;
    while (n < 25 && b[n]) {
        if (b[n] < 0x21u || b[n] > 0x7Eu) return 0;
        n++;
    }
    if (n < 1 || n >= 25) return 0;
    memcpy(out, b, (size_t)n);
    out[n] = 0;
    return 1;
}

static void census(uint32_t frame)
{
    static uint8_t *buf;
    static size_t bufsz = 4u << 20;
    int i;
    if (mem_fd < 0) {
        mem_fd = open("/proc/self/mem", O_RDONLY);
        if (mem_fd < 0) return;
    }
    if (!buf) {
        buf = (uint8_t *)malloc(bufsz);
        if (!buf) return;
    }
    load_ranges();                    /* the map can change between frames */
    for (i = 0; i < write_count; i++) {
        uint32_t a, hi = write_hi[i];
        if (hi - write_lo[i] > 0x20000000u) continue;   /* skip very large regions */
        for (a = write_lo[i]; a < hi; a += (uint32_t)bufsz) {
            size_t want = (size_t)(hi - a);
            ssize_t n;
            size_t j;
            if (want > bufsz) want = bufsz;
            n = pread(mem_fd, buf, want, (off_t)a);
            if (n <= 0) break;
            for (j = 0; j + 4u <= (size_t)n; j += 4u) {
                uint32_t vt = (uint32_t)buf[j] | ((uint32_t)buf[j + 1] << 8) |
                              ((uint32_t)buf[j + 2] << 16) | ((uint32_t)buf[j + 3] << 24);
                int kind;
                uint32_t toff, obj, tp, uid, owner, tgt, tgt_uid;
                int32_t hp;
                uint8_t ob[0x600];
                char id[26];
                if (vt == 0x79cbbcu) { kind = 1; toff = 0x418u; }
                else if (vt == 0x7a3540u) { kind = 2; toff = 0x5A8u; }
                else if (vt == 0x7addf8u) { kind = 3; toff = 0x5ACu; }
                else if (vt == 0x79b12cu) { kind = 4; toff = 0x5A8u; }
                else continue;
                obj = a + (uint32_t)j;
                if (pread(mem_fd, ob, sizeof ob, (off_t)obj) != (ssize_t)sizeof ob) continue;
                hp = (int32_t)((uint32_t)ob[0x6C] | ((uint32_t)ob[0x6D] << 8) |
                               ((uint32_t)ob[0x6E] << 16) | ((uint32_t)ob[0x6F] << 24));
                if (hp <= 0) continue;
                if (ob[0x7D] != 1) continue;
                uid = (uint32_t)ob[0x10] | ((uint32_t)ob[0x11] << 8) |
                      ((uint32_t)ob[0x12] << 16) | ((uint32_t)ob[0x13] << 24);
                if (uid == 1032246u && kind == 2) gi_addr = obj;
                owner = (uint32_t)ob[0x1B4] | ((uint32_t)ob[0x1B5] << 8) |
                        ((uint32_t)ob[0x1B6] << 16) | ((uint32_t)ob[0x1B7] << 24);
                tp = (uint32_t)ob[toff] | ((uint32_t)ob[toff + 1] << 8) |
                     ((uint32_t)ob[toff + 2] << 16) | ((uint32_t)ob[toff + 3] << 24);
                strcpy(id, "?");
                if (tp && pread_id(mem_fd, tp + 0x24, id)) { /* got the name */ }
                tgt = (uint32_t)ob[0x228] | ((uint32_t)ob[0x229] << 8) |
                      ((uint32_t)ob[0x22A] << 16) | ((uint32_t)ob[0x22B] << 24);
                tgt_uid = tgt ? pread_u32(mem_fd, tgt + 0x10) : 0;
                fprintf(f_objects,
                        "%u,%s,0x%08x,%u,%s,%u,%d,%d,0x%08x,%u,%d,%d,%d,%d,%u,%u,%u\n",
                        frame, kind_name(kind), obj, uid, id, owner, hp,
                        (int32_t)((uint32_t)ob[0x98] | ((uint32_t)ob[0x99] << 8) |
                                  ((uint32_t)ob[0x9A] << 16) | ((uint32_t)ob[0x9B] << 24)),
                        tgt, tgt_uid,
                        (int32_t)((uint32_t)ob[0xE0] | ((uint32_t)ob[0xE1] << 8) |
                                  ((uint32_t)ob[0xE2] << 16) | ((uint32_t)ob[0xE3] << 24)),
                        (int32_t)((uint32_t)ob[0x88] | ((uint32_t)ob[0x89] << 8) |
                                  ((uint32_t)ob[0x8A] << 16) | ((uint32_t)ob[0x8B] << 24)),
                        (int32_t)((uint32_t)ob[0x8C] | ((uint32_t)ob[0x8D] << 8) |
                                  ((uint32_t)ob[0x8E] << 16) | ((uint32_t)ob[0x8F] << 24)),
                        (int32_t)((uint32_t)ob[0x90] | ((uint32_t)ob[0x91] << 8) |
                                  ((uint32_t)ob[0x92] << 16) | ((uint32_t)ob[0x93] << 24)),
                        (uint32_t)((uint32_t)ob[0x2E0] | ((uint32_t)ob[0x2E1] << 8)),
                        (uint32_t)ob[0x57D], (uint32_t)ob[0x5C3]);
                objects_written++;
            }
        }
    }
    fflush(f_objects);
}

/* ------------------------------------------------------------------ */
/* hook 1: the frame boundary                                          */
/* ------------------------------------------------------------------ */

void join_sampler_frame(int site)
{
    uint32_t frame;
    const uint8_t *reg;
    char h410[65], h1016[65], hrng[65], h1012[65], hidx[65], htab[65], htail[65];
    if (enabled == -1) {
        enabled = getenv("JOIN_SAMPLER_OUT") != NULL;
        if (enabled) sampler_open();
    }
    if (!enabled) return;
    frame = MEM32(0xA40D2C);
    if (frame == 0u) return;
    if (frame <= last_frame) { duplicate_hits++; return; }
    last_frame = frame;
    uint32_t scenario = MEM32(0xa3d290u);
    if (!scenario) exit(81);
    uint32_t rng_va = scenario + 0x218u;
    reg = (const uint8_t *)(uintptr_t)ADDR(rng_va);
    sha256_hex(reg, RNG_LEN, h410);
    sha256_hex(reg, 0x3F8u, h1016);
    sha256_hex(reg + 4, 1008u, hrng);
    sha256_hex(reg, 0x3F4u, h1012);
    sha256_hex(reg + 4, 8u, hidx);            /* Index1 + Index2 */
    sha256_hex(reg + 12, 1000u, htab);         /* Table[250] */
    sha256_hex(reg + 1012, 4u, htail);         /* the four bytes before the tail */
    fprintf(f_frames, "%u,%d,%s,%s,%s,%s,0x%08x,%s,%s,%s,%u,0x%08x,0x%08x,0x%08x,%u,%u\n", frame, site, h410, h1016, hrng,
            h1012, MEM32(rng_va), hidx, htab, htail, reg[0], scenario, rng_va, MEM32(0xa40d3c), MEM32(0xa40b18), MEM32(0xa6eb1c));
    fflush(f_frames);
    {   /* the raw region, so a difference can be located byte by byte */
        uint8_t hdr[4];
        hdr[0] = (uint8_t)(frame & 0xFFu); hdr[1] = (uint8_t)((frame >> 8) & 0xFFu);
        hdr[2] = (uint8_t)((frame >> 16) & 0xFFu); hdr[3] = (uint8_t)((frame >> 24) & 0xFFu);
        fwrite(hdr, 1, 4, f_regions);
        fwrite(reg, 1, RNG_LEN, f_regions);
    }
    fflush(f_regions);
    frames_written++;
    /* Match original: snapshot frame 1 at speed 5, then remove the frame wait. */
    MEM32(0xa40b18) = 0;
    if (census_every && (frame % (uint32_t)census_every) == 0u) {
        int in_window = (!census_to || ((int)frame >= census_from && (int)frame <= census_to));
        int period = in_window ? census_every : census_out;
        if (period > 0 && (frame % (uint32_t)period) == 0u) census(frame);
    }
    if (gi_addr && MEM32(gi_addr + 0x10) == 1032246u) {
        fprintf(f_health, "%u,1032246,%d,%u,0x%08x\n", frame, (int32_t)MEM32(gi_addr + 0x6c), MEM8(gi_addr + 0x7d), gi_addr);
        fflush(f_health);
    }
    if (frame == 9000u) { fflush(NULL); _exit(0); }
}

/* ------------------------------------------------------------------ */
/* hook 2: the health write of the damage method                      */
/* ------------------------------------------------------------------ */

void join_sampler_damage(uint32_t target, uint32_t attacker, int old_health, int site)
{
    char tkind[16], tid[26], thouse[26], akind[16], aid[26], ahouse[26];
    uint32_t tuid, towner, auid, aowner, ttp, atp;
    int32_t new_health, damage;
    struct obj_detail td, ad;
    const char *site_name = (site == 1) ? "0x005d3dc7" : (site == 2) ? "0x005d3e65" : "0x005d3f11";
    if (enabled == -1) {
        enabled = getenv("JOIN_SAMPLER_OUT") != NULL;
        if (enabled) sampler_open();
    }
    if (!enabled) return;
    if (!target) return;
    new_health = (int32_t)MEM32(target + 0x6C);
    damage = old_health - new_health;
    obj_info(target, tkind, tid, thouse, &tuid, &towner, &ttp);
    obj_info(attacker, akind, aid, ahouse, &auid, &aowner, &atp);
    obj_detail_of(target, &td);
    obj_detail_of(attacker, &ad);
    fprintf(f_damage,
            "{\"frame\":%u,\"site\":\"%s\",\"target\":\"0x%08x\",\"target_uid\":%u,"
            "\"target_type\":\"%s\",\"target_kind\":\"%s\",\"target_house\":\"%s\",\"target_owner\":%u,"
            "\"target_type_ptr\":\"0x%08x\",\"target_alive\":%u,"
            "\"target_x\":%d,\"target_y\":%d,\"target_z\":%d,\"target_mission\":%d,\"target_stage\":%d,"
            "\"target_target\":\"0x%08x\",\"target_target_uid\":%u,"
            "\"attacker\":\"0x%08x\",\"attacker_uid\":%u,"
            "\"attacker_type\":\"%s\",\"attacker_kind\":\"%s\",\"attacker_house\":\"%s\",\"attacker_owner\":%u,"
            "\"attacker_type_ptr\":\"0x%08x\",\"attacker_alive\":%u,"
            "\"attacker_x\":%d,\"attacker_y\":%d,\"attacker_z\":%d,\"attacker_mission\":%d,"
            "\"attacker_target\":\"0x%08x\",\"attacker_target_uid\":%u,"
            "\"damage\":%d,\"old_health\":%d,\"health\":%d}\n",
            MEM32(0xA40D2C), site_name, target, tuid, tid, tkind, thouse, towner, ttp,
            (uint32_t)MEM8(target + 0x7D),
            td.x, td.y, td.z, td.mission, td.stage, td.target, td.target_uid,
            attacker, auid, aid, akind, ahouse, aowner, atp,
            (attacker ? (uint32_t)MEM8(attacker + 0x7D) : 0u),
            ad.x, ad.y, ad.z, ad.mission, ad.target, ad.target_uid,
            damage, old_health, new_health);
    fflush(f_damage);
    damage_written++;
}
