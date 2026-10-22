/*
 * libgpu replacement on top of the software GPU (gpu.c).
 */
#include "psx.h"

#include <string.h>

extern uint16_t psx_vram[];
void psx_gpu_exec(const uint32_t *p, int len);
void psx_gpu_reset(void);
void psx_gpu_set_tpage(uint32_t v);
void psx_gpu_fill(int x, int y, int w, int h, uint32_t rgb);
void psx_gpu_load(int x, int y, int w, int h, const uint16_t *src);
void psx_gpu_store(int x, int y, int w, int h, uint16_t *dst);
void psx_gpu_move(int sx, int sy, int dx, int dy, int w, int h);
const uint16_t *psx_gpu_display(int *stride, int *shift);

static DISPENV disp_env;
static int     disp_enabled;
static void  (*draw_sync_cb)(void);
static int     draw_done_pending;

#define TAG_LEN(t)   ((t) >> 24)
#define TAG_ADDR(t)  ((t) & 0xFFFFFF)
#define TAG_END      0xFFFFFFu

static void set_len(u_long *tag, unsigned len)
{
    *tag = (*tag & 0xFFFFFF) | ((u_long)len << 24);
}

/* ---- presentation ------------------------------------------------------------- */

void psx_gpu_init(void)
{
    psx_gpu_reset();
    memset(psx_vram, 0, 1024 * 512 * sizeof(uint16_t));
    disp_env.disp.w = 320;
    disp_env.disp.h = 240;
}

void psx_gpu_vblank(void)
{
    int stride, shift;
    const uint16_t *vram = psx_gpu_display(&stride, &shift);
    if (disp_env.isrgb24) {
        vram = psx_vram;
        stride = 1024;
        shift = 0;
    }
    plat_present(vram, stride, shift, disp_env.disp.x, disp_env.disp.y, disp_env.disp.w,
                 disp_env.disp.h, disp_env.isrgb24, disp_enabled,
                 psx_gte_widescreen && psx_ws_movie_active());
}

void psx_draw_sync_interrupt(void)
{
    if (draw_done_pending) {
        draw_done_pending = 0;
        if (draw_sync_cb)
            draw_sync_cb();
    }
}

/* ---- setup ---------------------------------------------------------------------- */

int ResetGraph(int mode)
{
    if (mode == 0 || mode == 3)
        psx_gpu_reset();
    draw_done_pending = 0;
    return 0;
}

int SetGraphDebug(int level) { (void)level; return 0; }

void SetDispMask(int mask) { disp_enabled = mask; }

DISPENV *PutDispEnv(DISPENV *env)
{
    disp_env = *env;
    return env;
}

static uint32_t texwindow_word(const RECT *tw)
{
    if (tw->w == 0 && tw->h == 0)
        return 0xE2000000u;
    return 0xE2000000u
         | ((((uint32_t)(-tw->w) & 0xFF) >> 3) << 0)
         | ((((uint32_t)(-tw->h) & 0xFF) >> 3) << 5)
         | ((((uint32_t)tw->x & 0xFF) >> 3) << 10)
         | ((((uint32_t)tw->y & 0xFF) >> 3) << 15);
}

/* GP0 words of a drawing environment; returns the count */
static int drawenv_words(const DRAWENV *env, uint32_t *w)
{
    int n = 0;
    int x1 = env->clip.x + env->clip.w - 1, y1 = env->clip.y + env->clip.h - 1;
    w[n++] = 0xE3000000u | (env->clip.x & 0x3FF) | ((env->clip.y & 0x3FF) << 10);
    w[n++] = 0xE4000000u | (x1 & 0x3FF) | ((y1 & 0x3FF) << 10);
    w[n++] = 0xE5000000u | (env->ofs[0] & 0x7FF) | ((env->ofs[1] & 0x7FF) << 11);
    w[n++] = 0xE1000000u | (env->tpage & 0x9FF) | ((env->dtd & 1) << 9) | ((env->dfe & 1) << 10);
    w[n++] = texwindow_word(&env->tw);
    w[n++] = 0xE6000000u;
    if (env->isbg) {
        w[n++] = 0x02000000u | env->r0 | (env->g0 << 8) | (env->b0 << 16);
        w[n++] = (env->clip.x & 0x3FF) | ((env->clip.y & 0x1FF) << 16);
        w[n++] = (env->clip.w & 0x3FF) | ((env->clip.h & 0x1FF) << 16);
    }
    return n;
}

DRAWENV *PutDrawEnv(DRAWENV *env)
{
    uint32_t w[16];
    int n = drawenv_words(env, w);
    psx_gpu_exec(w, n);
    return env;
}

void SetDrawEnv(DR_ENV *dr, DRAWENV *env)
{
    uint32_t w[16];
    int n = drawenv_words(env, w), i;
    for (i = 0; i < n; i++)
        dr->code[i] = w[i];
    set_len(&dr->tag, (unsigned)n);
}

/* ---- primitive helpers ----------------------------------------------------------- */

typedef struct { u_long tag; u_long code[2]; } DR_2;
typedef struct { u_long tag; u_long code[1]; } DR_1;

void SetDrawArea(void *p, RECT *r)
{
    DR_2 *d = (DR_2 *)p;
    int x1 = r->x + r->w - 1, y1 = r->y + r->h - 1;
    d->code[0] = 0xE3000000u | (r->x & 0x3FF) | ((r->y & 0x3FF) << 10);
    d->code[1] = 0xE4000000u | (x1 & 0x3FF) | ((y1 & 0x3FF) << 10);
    set_len(&d->tag, 2);
}

void SetDrawTPage(void *p, int dfe, int dtd, int tpage)
{
    DR_1 *d = (DR_1 *)p;
    d->code[0] = 0xE1000000u | (tpage & 0x9FF) | ((dtd & 1) << 9) | ((dfe & 1) << 10);
    set_len(&d->tag, 1);
}

void SetDrawStp(void *p, int pbw)
{
    DR_2 *d = (DR_2 *)p;
    d->code[0] = 0xE6000000u | (pbw & 1);
    d->code[1] = 0;
    set_len(&d->tag, 2);
}

u_short GetTPage(int tp, int abr, int x, int y)
{
    return (u_short)(((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2));
}

void SetPolyF4(void *p)
{
    u_char *b = (u_char *)p;
    set_len((u_long *)p, 5);
    b[7] = 0x28;
}

void SetSemiTrans(void *p, int abe)
{
    u_char *b = (u_char *)p;
    if (abe)
        b[7] |= 2;
    else
        b[7] &= ~2;
}

/* ---- ordering tables --------------------------------------------------------------- */

u_long *ClearOTagR(u_long *ot, int n)
{
    int i;
    if (n <= 0)
        return ot;
    ot[0] = TAG_END;
    for (i = 1; i < n; i++)
        ot[i] = (u_long)((uintptr_t)&ot[i - 1] & 0xFFFFFF);
    return ot;
}

u_long *ClearOTag(u_long *ot, int n)
{
    int i;
    for (i = 0; i < n - 1; i++)
        ot[i] = (u_long)((uintptr_t)&ot[i + 1] & 0xFFFFFF);
    if (n > 0)
        ot[n - 1] = TAG_END;
    return ot;
}

void DrawOTag(u_long *p)
{
    psx_ws_new_frame();
    unsigned guard = 0;
    while (p) {
        u_long tag = *p;
        unsigned len = TAG_LEN(tag);
        if (len)
            psx_gpu_exec((const uint32_t *)(p + 1), (int)len);
        if (TAG_ADDR(tag) == TAG_END)
            break;
        p = (u_long *)psx_decode_addr24(TAG_ADDR(tag));
        if (++guard > 1000000) {
            psx_log("DrawOTag: ordering table does not terminate\n");
            break;
        }
    }
    draw_done_pending = 1;
}

void DrawPrim(void *p)
{
    u_long tag = *(u_long *)p;
    psx_gpu_exec((const uint32_t *)((u_long *)p + 1), (int)TAG_LEN(tag));
}

int DrawSync(int mode)
{
    (void)mode;
    return 0;
}

u_long DrawSyncCallback(void (*func)(void))
{
    void (*old)(void) = draw_sync_cb;
    draw_sync_cb = func;
    return (u_long)(uintptr_t)old;
}

/* ---- VRAM transfers ---------------------------------------------------------------- */

int ClearImage(RECT *r, u_char red, u_char green, u_char blue)
{
    psx_gpu_fill(r->x, r->y, r->w, r->h, red | (green << 8) | (blue << 16));
    return 0;
}

int LoadImage(RECT *r, u_long *p)
{
    psx_gpu_load(r->x, r->y, r->w, r->h, (const uint16_t *)p);
    return 0;
}

int LoadImage2(RECT *r, u_long *p) { return LoadImage(r, p); }

int StoreImage(RECT *r, u_long *p)
{
    psx_gpu_store(r->x, r->y, r->w, r->h, (uint16_t *)p);
    return 0;
}

int StoreImage2(RECT *r, u_long *p) { return StoreImage(r, p); }

int MoveImage(RECT *r, int x, int y)
{
    psx_gpu_move(r->x, r->y, x, y, r->w, r->h);
    return 0;
}

/* ---- more primitive helpers (used by stage overlays) ------------------------------ */

void SetSprt(void *p)    { set_len((u_long *)p, 4);  ((u_char *)p)[7] = 0x64; }
void SetTile(void *p)    { set_len((u_long *)p, 3);  ((u_char *)p)[7] = 0x60; }
void SetPolyGT4(void *p) { set_len((u_long *)p, 12); ((u_char *)p)[7] = 0x3C; }

typedef struct { u_long tag; u_long code[5]; } DR_MOVE5;

void SetDrawMove(void *p, RECT *r, int x, int y)
{
    DR_MOVE5 *d = (DR_MOVE5 *)p;
    set_len(&d->tag, 5);
    d->code[0] = 0x01000000u;
    d->code[1] = 0x80000000u;
    d->code[2] = ((u_long)(u_short)r->y << 16) | (u_short)r->x;
    d->code[3] = ((u_long)(u_short)y << 16) | (u_short)x;
    d->code[4] = ((u_long)(u_short)r->h << 16) | (u_short)r->w;
}

void SetPriority(void *p, int pbc, int pbw)
{
    DR_1 *d = (DR_1 *)p;
    d->code[0] = 0xE6000000u | ((pbc & 1) << 1) | (pbw & 1);
    set_len(&d->tag, 1);
}

/* merge p1 (directly following p0 in memory) into p0's packet */
int MargePrim(void *p0, void *p1)
{
    unsigned l0 = TAG_LEN(*(u_long *)p0), l1 = TAG_LEN(*(u_long *)p1);
    unsigned len = l0 + l1 + 1;
    if (len > 16)
        return -1;
    set_len((u_long *)p0, len);
    *(u_long *)p1 = 0; /* the old tag becomes a GPU nop */
    return 0;
}

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h)
{
    memset(env, 0, sizeof(*env));
    env->clip.x = (short)x;
    env->clip.y = (short)y;
    env->clip.w = (short)w;
    env->clip.h = (short)h;
    env->ofs[0] = (short)x;
    env->ofs[1] = (short)y;
    env->tpage = GetTPage(0, 0, 640, 0);
    env->dtd = 1;
    return env;
}

DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h)
{
    memset(env, 0, sizeof(*env));
    env->disp.x = (short)x;
    env->disp.y = (short)y;
    env->disp.w = (short)w;
    env->disp.h = (short)h;
    return env;
}

int BreakDraw(void) { return 0; }
