/*
 * 60 fps by frame interpolation (experimental, --60fps).
 *
 * The game logic runs at 30 ticks per second and counts in frames, so it
 * cannot simply run twice as often. Instead an extra picture is drawn
 * between two game frames in which every recognised 3D vertex sits halfway
 * between its previous and its current screen position.
 *
 * Recognising vertices works by value, like PGXP: every screen position the
 * GTE produces gets a key made of the address the rotation matrix was loaded
 * from (one per object, e.g. &obj->screen), how often that matrix was loaded
 * this frame, and the vertex number since the load. The game copies the
 * positions into its primitives with plain C code, so primitives are matched
 * by the vertex value. Ambiguous values, large movements and partially
 * recognised primitives are left alone.
 */
#include "psx.h"

#include <stdlib.h>
#include <string.h>

int psx_interp_enabled;

#define TABLE_BITS 16
#define TABLE_SIZE (1u << TABLE_BITS)
#define MAX_MOVE   64          /* larger jumps (camera cuts) are not blended */

typedef struct {
    uint32_t gen;
    uint32_t val;              /* screen position (y << 16 | x) */
    uint64_t key;
    int      ambiguous;        /* value produced by different vertices */
} ValEntry;

typedef struct {
    uint32_t gen;
    uint64_t key;
    uint32_t val;
} KeyEntry;

typedef struct {
    uint32_t gen;
    ValEntry by_val[TABLE_SIZE];
    KeyEntry by_key[TABLE_SIZE];
} Frame;

/* rec: being recorded, last: frame N (drawn last), prev: frame N - 1.
 * Allocated on demand: as static data they would push the executable past
 * the 16 MiB the GPU's 24-bit links can address. */
static Frame *frames;
static Frame *rec, *last, *prev;
static uint32_t generation = 1;

/* matrix load ordinals within the frame being recorded */
#define ORD_SIZE 4096
static struct { uint32_t gen, addr, count; } ordinals[ORD_SIZE];

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static uint32_t hash64(uint64_t k) { return hash32((uint32_t)k ^ hash32((uint32_t)(k >> 32))); }

void psx_interp_init(int enable)
{
    psx_interp_enabled = 0;
    free(frames);
    frames = NULL;
    if (!enable)
        return;
    frames = (Frame *)calloc(3, sizeof(Frame));
    if (!frames)
        return;
    psx_interp_enabled = 1;
    rec = &frames[0];
    last = &frames[1];
    prev = &frames[2];
    memset(ordinals, 0, sizeof(ordinals));
    generation = 1;
    rec->gen = generation++;
    last->gen = generation++;
    prev->gen = generation++;
}

uint32_t psx_interp_rt_ordinal(uint32_t addr)
{
    uint32_t h = hash32(addr) & (ORD_SIZE - 1), i;
    for (i = 0; i < ORD_SIZE; i++, h = (h + 1) & (ORD_SIZE - 1)) {
        if (ordinals[h].gen != rec->gen) {
            ordinals[h].gen = rec->gen;
            ordinals[h].addr = addr;
            ordinals[h].count = 0;
            return 0;
        }
        if (ordinals[h].addr == addr)
            return ++ordinals[h].count;
    }
    return 0;
}

/* the GTE projected a vertex */
void psx_interp_vertex(uint64_t key, uint32_t val)
{
    uint32_t h, i;

    h = hash64(key) & (TABLE_SIZE - 1);
    for (i = 0; i < 64; i++, h = (h + 1) & (TABLE_SIZE - 1)) {
        KeyEntry *e = &rec->by_key[h];
        if (e->gen != rec->gen || e->key == key) {
            e->gen = rec->gen;
            e->key = key;
            e->val = val;
            break;
        }
    }
    h = hash32(val) & (TABLE_SIZE - 1);
    for (i = 0; i < 64; i++, h = (h + 1) & (TABLE_SIZE - 1)) {
        ValEntry *e = &rec->by_val[h];
        if (e->gen != rec->gen) {
            e->gen = rec->gen;
            e->val = val;
            e->key = key;
            e->ambiguous = 0;
            return;
        }
        if (e->val == val) {
            if (e->key != key)
                e->ambiguous = 1;
            return;
        }
    }
}

static int find_key(const Frame *f, uint32_t val, uint64_t *key)
{
    uint32_t h = hash32(val) & (TABLE_SIZE - 1), i;
    for (i = 0; i < 64; i++, h = (h + 1) & (TABLE_SIZE - 1)) {
        const ValEntry *e = &f->by_val[h];
        if (e->gen != f->gen)
            return 0;
        if (e->val == val) {
            *key = e->key;
            return !e->ambiguous;
        }
    }
    return 0;
}

static int find_val(const Frame *f, uint64_t key, uint32_t *val)
{
    uint32_t h = hash64(key) & (TABLE_SIZE - 1), i;
    for (i = 0; i < 64; i++, h = (h + 1) & (TABLE_SIZE - 1)) {
        const KeyEntry *e = &f->by_key[h];
        if (e->gen != f->gen)
            return 0;
        if (e->key == key) {
            *val = e->val;
            return 1;
        }
    }
    return 0;
}

/* position halfway between frame N - 1 and frame N for a vertex of frame N */
int psx_interp_vertex_mid(uint32_t val, uint32_t *mid)
{
    uint64_t key;
    uint32_t old;
    int x0, y0, x1, y1;

    if (!find_key(last, val, &key) || !find_val(prev, key, &old))
        return 0;
    x1 = (int16_t)val;
    y1 = (int16_t)(val >> 16);
    x0 = (int16_t)old;
    y0 = (int16_t)(old >> 16);
    if (x1 - x0 > MAX_MOVE || x0 - x1 > MAX_MOVE || y1 - y0 > MAX_MOVE || y0 - y1 > MAX_MOVE)
        return 0;
    *mid = (uint16_t)((x0 + x1) >> 1) | ((uint32_t)(uint16_t)((y0 + y1) >> 1) << 16);
    return 1;
}

/* a game frame was handed to the GPU: what was recorded becomes frame N */
void psx_interp_frame_done(void)
{
    Frame *f = prev;
    prev = last;
    last = rec;
    rec = f;
    rec->gen = generation++;
    if (generation == 0)
        generation = 1;
}
