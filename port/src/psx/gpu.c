/*
 * Software implementation of the PlayStation GPU (GP0 command set).
 *
 * VRAM is a 1024x512 array of 15-bit pixels, exactly like on the console, so
 * texture uploads, render-to-texture tricks and framebuffer effects behave as
 * the game expects. Rasterization follows the hardware rules: drawing offset
 * and area clipping, top-left fill convention, affine texture mapping,
 * texture windows, CLUTs, the four semi-transparency modes, texture
 * modulation, dithering and the mask bit.
 *
 * Higher resolution: with a render scale of 2 or 4 every primitive is drawn
 * twice - into the original VRAM, which stays authoritative for CPU reads,
 * palettes and indexed textures, and into an upscaled copy that is shown on
 * screen. 15-bit textures are sampled from the upscaled copy, so effects that
 * render into textures keep their detail.
 */
#include "psx.h"

#include <stdlib.h>
#include <string.h>

typedef int32_t  s32;
typedef int64_t  s64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

#define VRAM_W 1024
#define VRAM_H 512

uint16_t *psx_vram;     /* on the heap, see PSX_HEAP_BUFFER */

#define PIX(x, y) psx_vram[((y) & (VRAM_H - 1)) * VRAM_W + ((x) & (VRAM_W - 1))]

/* a drawing target: the original VRAM or its upscaled copy */
typedef struct {
    uint16_t *pix;
    int       shift;            /* log2 of the scale */
    int       stride;           /* pixels per row */
    int       wmask, hmask;
} Target;

static Target native = { NULL, 0, VRAM_W, VRAM_W - 1, VRAM_H - 1 };
static Target hires;            /* pix == NULL when the scale is 1 */

__attribute__((constructor)) static void vram_alloc(void)
{
    if (!(psx_vram = calloc(VRAM_W * VRAM_H, sizeof(uint16_t))))
        abort();
    native.pix = psx_vram;
}

#define TPIX(t, x, y) (t)->pix[((y) & (t)->hmask) * (t)->stride + ((x) & (t)->wmask)]

/* ---- drawing state (GP0 E1..E6) ----------------------------------------------- */

static struct {
    u32 tpage;          /* E1 bits 0..11 */
    int dither;         /* E1 bit 9 */
    int tex_x, tex_y;   /* texture page base */
    int depth;          /* 0 = 4 bit, 1 = 8 bit, 2/3 = 15 bit */
    int abr;            /* semi-transparency mode */
    int tw_mask_x, tw_mask_y, tw_off_x, tw_off_y; /* texture window (pixels) */
    int clip_x0, clip_y0, clip_x1, clip_y1;       /* drawing area (inclusive) */
    int off_x, off_y;   /* drawing offset */
    int set_mask;       /* E6 bit 0 */
    int check_mask;     /* E6 bit 1 */
} gs;

void psx_gpu_set_tpage(u32 v)
{
    gs.tpage = v & 0x7FF;
    gs.tex_x = (v & 0xF) * 64;
    gs.tex_y = ((v >> 4) & 1) * 256;
    gs.abr = (v >> 5) & 3;
    gs.depth = (v >> 7) & 3;
    gs.dither = (v >> 9) & 1;
}

static void set_texwindow(u32 v)
{
    gs.tw_mask_x = (v & 0x1F) * 8;
    gs.tw_mask_y = ((v >> 5) & 0x1F) * 8;
    gs.tw_off_x = ((v >> 10) & 0x1F) * 8;
    gs.tw_off_y = ((v >> 15) & 0x1F) * 8;
}

/* ---- widescreen: 2D elements are squeezed to 3/4 around the area centre ---- */

static int ws_squeeze;          /* set per primitive by the exec functions */
int psx_gpu_force3d;            /* set while drawing interpolated 3D primitives */

/* x in target units (already offset and scaled) */
static inline s32 squeeze(const Target *t, s32 x)
{
    s32 cx = ((gs.clip_x0 + gs.clip_x1 + 1) << t->shift) / 2;
    return cx + (((x - cx) * 3) >> 2);
}

/* 3D if at least two vertices were projected by the GTE (see ws.c) */
static int is_2d(const u32 *verts[], int n)
{
    int i, hits = 0;
    if (!psx_ws_hud || psx_ws_movie_active() || psx_gpu_force3d)
        return 0;
    for (i = 0; i < n; i++) {
        u32 w = *verts[i];
        if (psx_ws_is_projected((s32)(w << 21) >> 21, (s32)((w >> 16) << 21) >> 21))
            hits++;
    }
    return hits < (n < 2 ? n : 2);
}

/* ---- render scale ---------------------------------------------------------------- */

int psx_gpu_set_scale(int scale)
{
    int shift = scale >= 4 ? 2 : scale >= 2 ? 1 : 0;
    free(hires.pix);
    memset(&hires, 0, sizeof(hires));
    if (shift) {
        hires.shift = shift;
        hires.stride = VRAM_W << shift;
        hires.wmask = (VRAM_W << shift) - 1;
        hires.hmask = (VRAM_H << shift) - 1;
        hires.pix = (uint16_t *)calloc((size_t)hires.stride * (VRAM_H << shift), sizeof(uint16_t));
        if (!hires.pix) {
            memset(&hires, 0, sizeof(hires));
            shift = 0;
        }
    }
    return 1 << shift;
}

/* VRAM to display: the upscaled copy if there is one */
const uint16_t *psx_gpu_display(int *stride, int *shift)
{
    const Target *t = hires.pix ? &hires : &native;
    *stride = t->stride;
    *shift = t->shift;
    return t->pix;
}

/* ---- pixel pipeline ------------------------------------------------------------- */

static const s32 dither_tab[4][4] = {
    { -4,  0, -3,  1 },
    {  2, -2,  3, -1 },
    { -3,  1, -4,  0 },
    {  3, -1,  2, -2 },
};

static inline s32 clamp(s32 v, s32 lo, s32 hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static inline u16 blend(u16 back, u16 front, int mode)
{
    s32 br = back & 0x1F, bg = (back >> 5) & 0x1F, bb = (back >> 10) & 0x1F;
    s32 fr = front & 0x1F, fg = (front >> 5) & 0x1F, fb = (front >> 10) & 0x1F;
    s32 r, g, b;
    switch (mode) {
    case 0: r = (br + fr) >> 1; g = (bg + fg) >> 1; b = (bb + fb) >> 1; break;
    case 1: r = br + fr; g = bg + fg; b = bb + fb; break;
    case 2: r = br - fr; g = bg - fg; b = bb - fb; break;
    default: r = br + (fr >> 2); g = bg + (fg >> 2); b = bb + (fb >> 2); break;
    }
    r = clamp(r, 0, 31);
    g = clamp(g, 0, 31);
    b = clamp(b, 0, 31);
    return (u16)(r | (g << 5) | (b << 10) | (front & 0x8000));
}

/* write one pixel; semi = semi-transparency requested for this pixel */
static inline void plot(const Target *t, int x, int y, u16 c, int semi)
{
    u16 *p = &TPIX(t, x, y);
    if (gs.check_mask && (*p & 0x8000))
        return;
    if (semi)
        c = blend(*p, c, gs.abr);
    if (gs.set_mask)
        c |= 0x8000;
    *p = c;
}

/* 8-bit colour (with optional dithering) to 15 bit */
static inline u16 rgb15(s32 r, s32 g, s32 b, int x, int y, int dither)
{
    if (dither) {
        s32 d = dither_tab[y & 3][x & 3];
        r += d; g += d; b += d;
    }
    r = clamp(r, 0, 255);
    g = clamp(g, 0, 255);
    b = clamp(b, 0, 255);
    return (u16)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
}

typedef struct {
    int tex_x, tex_y, depth;
    int clut_x, clut_y;
} TexState;

static inline int wrap_u(int u) { u &= 0xFF; return (u & ~gs.tw_mask_x) | (gs.tw_off_x & gs.tw_mask_x); }
static inline int wrap_v(int v) { v &= 0xFF; return (v & ~gs.tw_mask_y) | (gs.tw_off_y & gs.tw_mask_y); }

/* texel at texture coordinates in target units (u, v scaled by the target) */
static inline u16 texel(const Target *t, const TexState *ts, int uh, int vh)
{
    int s = t->shift;
    int u = wrap_u(uh >> s), v = wrap_v(vh >> s);
    switch (ts->depth) {
    case 0: {
        u16 w = PIX(ts->tex_x + (u >> 2), ts->tex_y + v);
        return PIX(ts->clut_x + ((w >> ((u & 3) * 4)) & 0xF), ts->clut_y);
    }
    case 1: {
        u16 w = PIX(ts->tex_x + (u >> 1), ts->tex_y + v);
        return PIX(ts->clut_x + ((w >> ((u & 1) * 8)) & 0xFF), ts->clut_y);
    }
    default:
        if (s) {
            int sub = (1 << s) - 1;
            return TPIX(t, ((ts->tex_x + u) << s) | (uh & sub), ((ts->tex_y + v) << s) | (vh & sub));
        }
        return PIX(ts->tex_x + u, ts->tex_y + v);
    }
}

/* texel * vertex colour / 128 */
static inline u16 modulate(u16 tx, s32 r, s32 g, s32 b, int x, int y, int dither)
{
    s32 tr = (tx & 0x1F) << 3, tg = ((tx >> 5) & 0x1F) << 3, tb = ((tx >> 10) & 0x1F) << 3;
    return (u16)(rgb15((tr * r) >> 7, (tg * g) >> 7, (tb * b) >> 7, x, y, dither) | (tx & 0x8000));
}

/* ---- triangles -------------------------------------------------------------------- */

typedef struct {
    s32 x, y;
    s32 r, g, b;
    s32 u, v;
} Vtx;

enum { F_GOURAUD = 1, F_TEXTURED = 2, F_SEMI = 4, F_RAW = 8 };

static inline s64 floor_div(s64 a, s64 b)
{
    s64 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

/* attribute plane: value(x, y) = base + dx * x + dy * y, 16.16 fixed point */
typedef struct { s64 base, dx, dy; } Plane;

static Plane plane(const Vtx *v0, const Vtx *v1, const Vtx *v2, s32 a0, s32 a1, s32 a2, s64 area)
{
    Plane p;
    s64 nx = (s64)(a1 - a0) * (v2->y - v0->y) - (s64)(a2 - a0) * (v1->y - v0->y);
    s64 ny = (s64)(a2 - a0) * (v1->x - v0->x) - (s64)(a1 - a0) * (v2->x - v0->x);
    p.dx = (nx << 16) / area;
    p.dy = (ny << 16) / area;
    p.base = ((s64)a0 << 16) - p.dx * v0->x - p.dy * v0->y + (1 << 15);
    return p;
}

static void draw_triangle(const Target *t, Vtx v0, Vtx v1, Vtx v2, int flags, const TexState *tex)
{
    int s = t->shift, i;
    s64 area;
    s32 miny, maxy, cx0, cx1, y, minx, maxx;
    int dither;
    Vtx *v[3], tmp;
    Plane pr, pg, pb, pu, pv;
    struct { s64 a, b, c; } e[3];   /* edge: a*x + b*y + c >= 0 */

    v0.x += gs.off_x; v0.y += gs.off_y;
    v1.x += gs.off_x; v1.y += gs.off_y;
    v2.x += gs.off_x; v2.y += gs.off_y;

    /* the GPU skips polygons that are too large (checked in GPU pixels) */
    minx = v0.x < v1.x ? v0.x : v1.x; if (v2.x < minx) minx = v2.x;
    maxx = v0.x > v1.x ? v0.x : v1.x; if (v2.x > maxx) maxx = v2.x;
    miny = v0.y < v1.y ? v0.y : v1.y; if (v2.y < miny) miny = v2.y;
    maxy = v0.y > v1.y ? v0.y : v1.y; if (v2.y > maxy) maxy = v2.y;
    if (maxx - minx >= 1024 || maxy - miny >= 512)
        return;

    /* to target units */
    v0.x <<= s; v0.y <<= s; v0.u <<= s; v0.v <<= s;
    v1.x <<= s; v1.y <<= s; v1.u <<= s; v1.v <<= s;
    v2.x <<= s; v2.y <<= s; v2.u <<= s; v2.v <<= s;
    if (ws_squeeze) {
        v0.x = squeeze(t, v0.x);
        v1.x = squeeze(t, v1.x);
        v2.x = squeeze(t, v2.x);
    }

    area = (s64)(v1.x - v0.x) * (v2.y - v0.y) - (s64)(v1.y - v0.y) * (v2.x - v0.x);
    if (area == 0)
        return;
    if (area < 0) {
        tmp = v1; v1 = v2; v2 = tmp;
        area = -area;
    }
    v[0] = &v0; v[1] = &v1; v[2] = &v2;

    /* edge functions; the top-left rule decides who owns pixels on an edge */
    for (i = 0; i < 3; i++) {
        const Vtx *a = v[(i + 1) % 3], *b = v[(i + 2) % 3];
        s32 dx = b->x - a->x, dy = b->y - a->y;
        int top_left = (dy < 0) || (dy == 0 && dx > 0);
        e[i].a = -(s64)dy;
        e[i].b = dx;
        e[i].c = (s64)dy * a->x - (s64)dx * a->y - (top_left ? 0 : 1);
    }

    miny = v0.y < v1.y ? v0.y : v1.y; if (v2.y < miny) miny = v2.y;
    maxy = v0.y > v1.y ? v0.y : v1.y; if (v2.y > maxy) maxy = v2.y;
    if (miny < (gs.clip_y0 << s)) miny = gs.clip_y0 << s;
    if (maxy > ((gs.clip_y1 + 1) << s) - 1) maxy = ((gs.clip_y1 + 1) << s) - 1;
    cx0 = gs.clip_x0 << s;
    cx1 = ((gs.clip_x1 + 1) << s) - 1;

    dither = gs.dither && ((flags & F_GOURAUD) || ((flags & F_TEXTURED) && !(flags & F_RAW)));
    if (flags & F_GOURAUD) {
        pr = plane(&v0, &v1, &v2, v0.r, v1.r, v2.r, area);
        pg = plane(&v0, &v1, &v2, v0.g, v1.g, v2.g, area);
        pb = plane(&v0, &v1, &v2, v0.b, v1.b, v2.b, area);
    }
    if (flags & F_TEXTURED) {
        pu = plane(&v0, &v1, &v2, v0.u, v1.u, v2.u, area);
        pv = plane(&v0, &v1, &v2, v0.v, v1.v, v2.v, area);
    }

    for (y = miny; y <= maxy; y++) {
        /* span of x where all three edge functions are >= 0 */
        s64 xl = cx0, xr = cx1;
        s64 r = 0, g = 0, b = 0, u = 0, vv = 0;
        s32 x;
        for (i = 0; i < 3 && xl <= xr; i++) {
            s64 c = e[i].b * y + e[i].c;
            if (e[i].a > 0) {
                s64 lo = -floor_div(c, e[i].a);   /* ceil(-c / a) */
                if (lo > xl) xl = lo;
            } else if (e[i].a < 0) {
                s64 hi = floor_div(c, -e[i].a);
                if (hi < xr) xr = hi;
            } else if (c < 0) {
                xl = xr + 1;
            }
        }
        if (xl > xr)
            continue;

        if (flags & F_GOURAUD) {
            r = pr.base + pr.dx * xl + pr.dy * y;
            g = pg.base + pg.dx * xl + pg.dy * y;
            b = pb.base + pb.dx * xl + pb.dy * y;
        }
        if (flags & F_TEXTURED) {
            u = pu.base + pu.dx * xl + pu.dy * y;
            vv = pv.base + pv.dx * xl + pv.dy * y;
        }

        for (x = (s32)xl; x <= (s32)xr; x++) {
            s32 cr, cg, cb;
            u16 c;
            int semi;

            if (flags & F_GOURAUD) {
                cr = (s32)(r >> 16); cg = (s32)(g >> 16); cb = (s32)(b >> 16);
                r += pr.dx; g += pg.dx; b += pb.dx;
            } else {
                cr = v0.r; cg = v0.g; cb = v0.b;
            }

            if (flags & F_TEXTURED) {
                u16 tx = texel(t, tex, (int)(u >> 16), (int)(vv >> 16));
                u += pu.dx;
                vv += pv.dx;
                if (tx == 0)
                    continue;
                c = (flags & F_RAW) ? tx : modulate(tx, cr, cg, cb, x >> s, y >> s, dither);
                semi = (flags & F_SEMI) && (tx & 0x8000);
            } else {
                c = rgb15(cr, cg, cb, x >> s, y >> s, dither);
                semi = (flags & F_SEMI) != 0;
            }
            plot(t, x, y, c, semi);
        }
    }
}

/* ---- lines ------------------------------------------------------------------------ */

static void draw_line(const Target *t, Vtx a, Vtx b, int flags)
{
    s32 dx, dy, steps, i, s = t->shift;
    int dither = gs.dither && (flags & F_GOURAUD);

    a.x += gs.off_x; a.y += gs.off_y;
    b.x += gs.off_x; b.y += gs.off_y;
    if (ws_squeeze) {
        a.x = squeeze(&native, a.x);
        b.x = squeeze(&native, b.x);
    }
    dx = b.x - a.x;
    dy = b.y - a.y;
    if (abs(dx) >= 1024 || abs(dy) >= 512)
        return;
    steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

    for (i = 0; i <= steps; i++) {
        s32 x, y, r, g, bl, j, k;
        u16 c;
        if (steps == 0) {
            x = a.x; y = a.y; r = a.r; g = a.g; bl = a.b;
        } else {
            x = a.x + (dx * i + (dx >= 0 ? steps / 2 : -steps / 2)) / steps;
            y = a.y + (dy * i + (dy >= 0 ? steps / 2 : -steps / 2)) / steps;
            if (flags & F_GOURAUD) {
                r = a.r + (b.r - a.r) * i / steps;
                g = a.g + (b.g - a.g) * i / steps;
                bl = a.b + (b.b - a.b) * i / steps;
            } else {
                r = a.r; g = a.g; bl = a.b;
            }
        }
        if (x < gs.clip_x0 || x > gs.clip_x1 || y < gs.clip_y0 || y > gs.clip_y1)
            continue;
        c = rgb15(r, g, bl, x, y, dither);
        /* one GPU pixel = a scale x scale block */
        for (j = 0; j < (1 << s); j++)
            for (k = 0; k < (1 << s); k++)
                plot(t, (x << s) + k, (y << s) + j, c, (flags & F_SEMI) != 0);
    }
}

/* ---- rectangles ------------------------------------------------------------------- */

static void draw_rect(const Target *t, s32 x0, s32 y0, s32 w, s32 h, s32 r, s32 g, s32 b,
                      int flags, const TexState *tex, int u0, int v0)
{
    s32 x, y, s = t->shift;
    s32 cx0 = gs.clip_x0 << s, cx1 = ((gs.clip_x1 + 1) << s) - 1;
    s32 cy0 = gs.clip_y0 << s, cy1 = ((gs.clip_y1 + 1) << s) - 1;

    x0 = (x0 + gs.off_x) << s;
    y0 = (y0 + gs.off_y) << s;
    w <<= s;
    h <<= s;
    if (ws_squeeze) {
        s32 x1 = squeeze(t, x0 + w);
        x0 = squeeze(t, x0);
        w = x1 - x0;
    }
    for (y = 0; y < h; y++) {
        s32 py = y0 + y;
        if (py < cy0 || py > cy1)
            continue;
        for (x = 0; x < w; x++) {
            s32 px = x0 + x;
            u16 c;
            int semi;
            if (px < cx0 || px > cx1)
                continue;
            if (flags & F_TEXTURED) {
                /* a squeezed sprite still shows its whole texture */
                s32 ux = ws_squeeze ? (x * 4) / 3 : x;
                u16 tx = texel(t, tex, (u0 << s) + ux, (v0 << s) + y);
                if (tx == 0)
                    continue;
                c = (flags & F_RAW) ? tx : modulate(tx, r, g, b, px >> s, py >> s, 0);
                semi = (flags & F_SEMI) && (tx & 0x8000);
            } else {
                c = rgb15(r, g, b, px >> s, py >> s, 0);
                semi = (flags & F_SEMI) != 0;
            }
            plot(t, px, py, c, semi);
        }
    }
}

/* ---- VRAM transfers (original VRAM + upscaled copy) ------------------------------------ */

static void fill_target(const Target *t, int x, int y, int w, int h, u16 c)
{
    int i, j, s = t->shift;
    for (j = 0; j < (h << s); j++)
        for (i = 0; i < (w << s); i++)
            TPIX(t, (x << s) + i, (y << s) + j) = c;
}

void psx_gpu_fill(int x, int y, int w, int h, u32 rgb)
{
    u16 c = rgb15(rgb & 0xFF, (rgb >> 8) & 0xFF, (rgb >> 16) & 0xFF, 0, 0, 0);
    fill_target(&native, x, y, w, h, c);
    if (hires.pix)
        fill_target(&hires, x, y, w, h, c);
}

void psx_gpu_load(int x, int y, int w, int h, const u16 *src)
{
    int i, j, k, l, s = hires.shift;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            u16 c = *src++;
            u16 *p = &PIX(x + i, y + j);
            if (gs.check_mask && (*p & 0x8000))
                continue;
            if (gs.set_mask)
                c |= 0x8000;
            *p = c;
            if (hires.pix)
                for (k = 0; k < (1 << s); k++)
                    for (l = 0; l < (1 << s); l++)
                        TPIX(&hires, ((x + i) << s) + l, ((y + j) << s) + k) = c;
        }
}

void psx_gpu_store(int x, int y, int w, int h, u16 *dst)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            *dst++ = PIX(x + i, y + j);
}

static void move_target(const Target *t, int sx, int sy, int dx, int dy, int w, int h)
{
    int s = t->shift, i, j;
    size_t n = (size_t)(w << s) * (size_t)(h << s);
    u16 *tmp = (u16 *)malloc(n * sizeof(u16));
    if (!tmp)
        return;
    for (j = 0; j < (h << s); j++)
        for (i = 0; i < (w << s); i++)
            tmp[j * (w << s) + i] = TPIX(t, (sx << s) + i, (sy << s) + j);
    for (j = 0; j < (h << s); j++)
        for (i = 0; i < (w << s); i++) {
            u16 *p = &TPIX(t, (dx << s) + i, (dy << s) + j);
            u16 c = tmp[j * (w << s) + i];
            if (gs.check_mask && (*p & 0x8000))
                continue;
            *p = gs.set_mask ? (u16)(c | 0x8000) : c;
        }
    free(tmp);
}

void psx_gpu_move(int sx, int sy, int dx, int dy, int w, int h)
{
    move_target(&native, sx, sy, dx, dy, w, h);
    if (hires.pix)
        move_target(&hires, sx, sy, dx, dy, w, h);
}

/* ---- GP0 command decoding ---------------------------------------------------------- */

static inline s32 sext11(u32 v) { return (s32)(v << 21) >> 21; }

static void vertex(u32 w, Vtx *v)
{
    v->x = sext11(w & 0x7FF);
    v->y = sext11((w >> 16) & 0x7FF);
}

static void color(u32 w, Vtx *v)
{
    v->r = w & 0xFF;
    v->g = (w >> 8) & 0xFF;
    v->b = (w >> 16) & 0xFF;
}

/* number of words of the command at p (polylines scan for the terminator) */
int psx_gpu_cmd_words(const u32 *p, int avail)
{
    u32 cmd = p[0] >> 24;
    if (cmd >= 0x20 && cmd < 0x40) {
        int quad = (cmd & 0x08) != 0, tex = (cmd & 0x04) != 0, gour = (cmd & 0x10) != 0;
        int n = quad ? 4 : 3;
        return 1 + n * (1 + tex) + (gour ? n - 1 : 0);
    }
    if (cmd >= 0x40 && cmd < 0x60) {
        int gour = (cmd & 0x10) != 0;
        if (cmd & 0x08) {
            int i = gour ? 3 : 2;
            while (i < avail && (p[i] & 0xF000F000u) != 0x50005000u)
                i += gour ? 2 : 1;
            return i + 1;
        }
        return gour ? 4 : 3;
    }
    if (cmd >= 0x60 && cmd < 0x80)
        return 2 + ((cmd & 0x04) ? 1 : 0) + (((cmd >> 3) & 3) == 0 ? 1 : 0);
    if (cmd >= 0x80 && cmd < 0xA0)
        return 4;
    if (cmd >= 0xA0 && cmd < 0xC0) {
        u32 wh = p[2];
        u32 w = wh & 0xFFFF, h = wh >> 16;
        return 3 + (int)((w * h + 1) / 2);
    }
    if (cmd >= 0xC0 && cmd < 0xE0)
        return 3;
    if (cmd == 0x02)
        return 3;
    return 1;
}

static void tex_from(u32 clut_word, u32 page_word, TexState *t, int use_page)
{
    u32 clut = clut_word >> 16;
    t->clut_x = (clut & 0x3F) * 16;
    t->clut_y = (clut >> 6) & 0x1FF;
    if (use_page) {
        u32 page = page_word >> 16;
        /* the polygon's tpage replaces E1 bits 0..8 and bit 11 */
        psx_gpu_set_tpage((gs.tpage & ~0x9FFu) | (page & 0x9FF) | (gs.dither << 9));
    }
    t->tex_x = gs.tex_x;
    t->tex_y = gs.tex_y;
    t->depth = gs.depth;
}

static void exec_poly(const u32 *p)
{
    u32 cmd = p[0] >> 24;
    int quad = (cmd & 0x08) != 0, tex = (cmd & 0x04) != 0, gour = (cmd & 0x10) != 0;
    int n = quad ? 4 : 3, i, k = 0;
    int flags = (gour ? F_GOURAUD : 0) | (tex ? F_TEXTURED : 0) | ((cmd & 2) ? F_SEMI : 0) | ((tex && (cmd & 1)) ? F_RAW : 0);
    Vtx v[4];
    TexState ts = { 0, 0, 0, 0, 0 };

    memset(v, 0, sizeof(v));
    for (i = 0; i < n; i++) {
        if (i == 0 || gour)
            color(p[k++], &v[i]);
        else
            v[i].r = v[0].r, v[i].g = v[0].g, v[i].b = v[0].b;
        vertex(p[k++], &v[i]);
        if (tex) {
            v[i].u = p[k] & 0xFF;
            v[i].v = (p[k] >> 8) & 0xFF;
            if (i == 0)
                tex_from(p[k], 0, &ts, 0);
            if (i == 1)
                tex_from(p[k - (gour ? 3 : 2)], p[k], &ts, 1);
            k++;
        }
    }
    /* raw textures ignore the vertex colour */
    if (flags & F_RAW)
        for (i = 0; i < n; i++)
            v[i].r = v[i].g = v[i].b = 128;

    {
        const u32 *vw[4];
        int step = 1 + tex, base = 1;
        for (i = 0; i < n; i++)
            vw[i] = &p[gour ? base + i * (step + 1) : base + i * step];
        ws_squeeze = is_2d(vw, n);
    }
    draw_triangle(&native, v[0], v[1], v[2], flags, &ts);
    if (quad)
        draw_triangle(&native, v[1], v[2], v[3], flags, &ts);
    if (hires.pix) {
        draw_triangle(&hires, v[0], v[1], v[2], flags, &ts);
        if (quad)
            draw_triangle(&hires, v[1], v[2], v[3], flags, &ts);
    }
}

static void exec_line(const u32 *p, int words)
{
    u32 cmd = p[0] >> 24;
    int gour = (cmd & 0x10) != 0;
    int flags = (gour ? F_GOURAUD : 0) | ((cmd & 2) ? F_SEMI : 0);
    Vtx a, b;
    int k = 0, prev;

    memset(&a, 0, sizeof(a));
    color(p[k++], &a);
    prev = k;                       /* index of the current start vertex word */
    vertex(p[k++], &a);
    while (k < words) {
        const u32 *vw[2];
        if ((p[k] & 0xF000F000u) == 0x50005000u && (cmd & 0x08))
            break;
        b = a;
        if (gour)
            color(p[k++], &b);
        if (k >= words)
            break;
        vw[0] = &p[prev];
        vw[1] = &p[k];
        prev = k;
        vertex(p[k++], &b);
        ws_squeeze = is_2d(vw, 2);
        draw_line(&native, a, b, flags);
        if (hires.pix)
            draw_line(&hires, a, b, flags);
        a = b;
        if (!(cmd & 0x08))
            break;
    }
}

static void exec_rect(const u32 *p)
{
    u32 cmd = p[0] >> 24;
    int tex = (cmd & 0x04) != 0;
    int size = (cmd >> 3) & 3;
    int flags = (tex ? F_TEXTURED : 0) | ((cmd & 2) ? F_SEMI : 0) | ((tex && (cmd & 1)) ? F_RAW : 0);
    Vtx v;
    int k = 1, u0 = 0, v0 = 0;
    s32 w, h;
    TexState ts = { 0, 0, 0, 0, 0 };

    memset(&v, 0, sizeof(v));
    color(p[0], &v);
    vertex(p[k++], &v);
    if (tex) {
        u0 = p[k] & 0xFF;
        v0 = (p[k] >> 8) & 0xFF;
        tex_from(p[k], 0, &ts, 0);
        k++;
    }
    switch (size) {
    case 0: w = p[k] & 0x3FF; h = (p[k] >> 16) & 0x1FF; break;
    case 1: w = h = 1; break;
    case 2: w = h = 8; break;
    default: w = h = 16; break;
    }
    if (flags & F_RAW)
        v.r = v.g = v.b = 128;
    /* sprites and tiles are HUD, text and menu elements */
    ws_squeeze = psx_ws_hud && !psx_ws_movie_active();
    draw_rect(&native, v.x, v.y, w, h, v.r, v.g, v.b, flags, &ts, u0, v0);
    if (hires.pix)
        draw_rect(&hires, v.x, v.y, w, h, v.r, v.g, v.b, flags, &ts, u0, v0);
}

static void exec_command(const u32 *p, int words)
{
    u32 cmd = p[0] >> 24;

    if (cmd >= 0x20 && cmd < 0x40) {
        exec_poly(p);
        return;
    }
    if (cmd >= 0x40 && cmd < 0x60) {
        exec_line(p, words);
        return;
    }
    if (cmd >= 0x60 && cmd < 0x80) {
        exec_rect(p);
        return;
    }
    if (cmd >= 0x80 && cmd < 0xA0) {
        psx_gpu_move(p[1] & 0x3FF, (p[1] >> 16) & 0x1FF, p[2] & 0x3FF, (p[2] >> 16) & 0x1FF,
                     ((p[3] & 0xFFFF) - 1) % 1024 + 1, (((p[3] >> 16) & 0xFFFF) - 1) % 512 + 1);
        return;
    }
    if (cmd >= 0xA0 && cmd < 0xC0) {
        psx_gpu_load(p[1] & 0x3FF, (p[1] >> 16) & 0x1FF, p[2] & 0xFFFF, p[2] >> 16, (const u16 *)&p[3]);
        return;
    }
    switch (cmd) {
    case 0x02: {
        int x = p[1] & 0x3F0, y = (p[1] >> 16) & 0x1FF;
        int w = ((p[2] & 0x3FF) + 15) & ~15, h = (p[2] >> 16) & 0x1FF;
        psx_gpu_fill(x, y, w, h, p[0] & 0xFFFFFF);
        break;
    }
    case 0xE1:
        psx_gpu_set_tpage(p[0]);
        break;
    case 0xE2:
        set_texwindow(p[0]);
        break;
    case 0xE3:
        gs.clip_x0 = p[0] & 0x3FF;
        gs.clip_y0 = (p[0] >> 10) & 0x3FF;
        break;
    case 0xE4:
        gs.clip_x1 = p[0] & 0x3FF;
        gs.clip_y1 = (p[0] >> 10) & 0x3FF;
        break;
    case 0xE5:
        gs.off_x = sext11(p[0] & 0x7FF);
        gs.off_y = sext11((p[0] >> 11) & 0x7FF);
        break;
    case 0xE6:
        gs.set_mask = p[0] & 1;
        gs.check_mask = (p[0] >> 1) & 1;
        break;
    default:
        break; /* nop, cache flush, VRAM->CPU (handled by StoreImage) */
    }
}

/* execute a packet body of `len` words */
void psx_gpu_exec(const u32 *p, int len)
{
    while (len > 0) {
        int n = psx_gpu_cmd_words(p, len);
        if (n > len)
            n = len;
        exec_command(p, n);
        p += n;
        len -= n;
    }
}

/* drawing state around out-of-band redraws (60 fps interpolation) */
static __typeof__(gs) saved_gs;

void psx_gpu_push_state(void) { saved_gs = gs; }
void psx_gpu_pop_state(void) { gs = saved_gs; }

void psx_gpu_reset(void)
{
    memset(&gs, 0, sizeof(gs));
    gs.clip_x1 = VRAM_W - 1;
    gs.clip_y1 = VRAM_H - 1;
}
