/*
 * Software implementation of the PlayStation GPU (GP0 command set).
 *
 * VRAM is a 1024x512 array of 15-bit pixels, exactly like on the console, so
 * texture uploads, render-to-texture tricks and framebuffer effects behave as
 * the game expects. Rasterization follows the hardware rules: drawing offset
 * and area clipping, top-left fill convention, affine texture mapping,
 * texture windows, CLUTs, the four semi-transparency modes, texture
 * modulation, dithering and the mask bit.
 */
#include "psx.h"

#include <stdlib.h>
#include <string.h>

typedef int32_t  s32;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

#define VRAM_W 1024
#define VRAM_H 512

uint16_t psx_vram[VRAM_W * VRAM_H];

#define PIX(x, y) psx_vram[((y) & (VRAM_H - 1)) * VRAM_W + ((x) & (VRAM_W - 1))]

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

/* ---- pixel pipeline ------------------------------------------------------------- */

static const s32 dither_tab[4][4] = {
    { -4,  0, -3,  1 },
    {  2, -2,  3, -1 },
    { -3,  1, -4,  0 },
    {  3, -1,  2, -2 },
};

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
    if (r < 0) r = 0; if (r > 31) r = 31;
    if (g < 0) g = 0; if (g > 31) g = 31;
    if (b < 0) b = 0; if (b > 31) b = 31;
    return (u16)(r | (g << 5) | (b << 10) | (front & 0x8000));
}

/* write one pixel; semi = semi-transparency requested for this pixel */
static inline void plot(int x, int y, u16 c, int semi)
{
    u16 *p = &PIX(x, y);
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
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (u16)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
}

typedef struct {
    int tex_x, tex_y, depth;
    int clut_x, clut_y;
} TexState;

static inline u16 texel(const TexState *t, int u, int v)
{
    u &= 0xFF;
    v &= 0xFF;
    u = (u & ~gs.tw_mask_x) | (gs.tw_off_x & gs.tw_mask_x);
    v = (v & ~gs.tw_mask_y) | (gs.tw_off_y & gs.tw_mask_y);
    switch (t->depth) {
    case 0: {
        u16 w = PIX(t->tex_x + (u >> 2), t->tex_y + v);
        int idx = (w >> ((u & 3) * 4)) & 0xF;
        return PIX(t->clut_x + idx, t->clut_y);
    }
    case 1: {
        u16 w = PIX(t->tex_x + (u >> 1), t->tex_y + v);
        int idx = (w >> ((u & 1) * 8)) & 0xFF;
        return PIX(t->clut_x + idx, t->clut_y);
    }
    default:
        return PIX(t->tex_x + u, t->tex_y + v);
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

static inline int64_t edge(const Vtx *a, const Vtx *b, s32 px, s32 py)
{
    return (int64_t)(b->x - a->x) * (py - a->y) - (int64_t)(b->y - a->y) * (px - a->x);
}

/* top-left rule: an edge owns its pixels if it is a top or left edge */
static inline int is_top_left(const Vtx *a, const Vtx *b)
{
    s32 dx = b->x - a->x, dy = b->y - a->y;
    return (dy < 0) || (dy == 0 && dx > 0);
}

static void draw_triangle(Vtx v0, Vtx v1, Vtx v2, int flags, const TexState *tex)
{
    int64_t area;
    s32 minx, maxx, miny, maxy, x, y;
    int bias0, bias1, bias2;
    int dither;
    Vtx t;

    v0.x += gs.off_x; v0.y += gs.off_y;
    v1.x += gs.off_x; v1.y += gs.off_y;
    v2.x += gs.off_x; v2.y += gs.off_y;

    area = edge(&v0, &v1, v2.x, v2.y);
    if (area == 0)
        return;
    if (area < 0) {
        t = v1; v1 = v2; v2 = t;
        area = -area;
    }

    /* the GPU skips polygons that are too large */
    minx = v0.x < v1.x ? v0.x : v1.x; if (v2.x < minx) minx = v2.x;
    maxx = v0.x > v1.x ? v0.x : v1.x; if (v2.x > maxx) maxx = v2.x;
    miny = v0.y < v1.y ? v0.y : v1.y; if (v2.y < miny) miny = v2.y;
    maxy = v0.y > v1.y ? v0.y : v1.y; if (v2.y > maxy) maxy = v2.y;
    if (maxx - minx >= 1024 || maxy - miny >= 512)
        return;

    if (minx < gs.clip_x0) minx = gs.clip_x0;
    if (miny < gs.clip_y0) miny = gs.clip_y0;
    if (maxx > gs.clip_x1) maxx = gs.clip_x1;
    if (maxy > gs.clip_y1) maxy = gs.clip_y1;
    if (minx > maxx || miny > maxy)
        return;

    bias0 = is_top_left(&v1, &v2) ? 0 : -1;
    bias1 = is_top_left(&v2, &v0) ? 0 : -1;
    bias2 = is_top_left(&v0, &v1) ? 0 : -1;

    dither = gs.dither && ((flags & F_GOURAUD) || ((flags & F_TEXTURED) && !(flags & F_RAW)));

    for (y = miny; y <= maxy; y++) {
        for (x = minx; x <= maxx; x++) {
            int64_t w0 = edge(&v1, &v2, x, y);
            int64_t w1 = edge(&v2, &v0, x, y);
            int64_t w2 = edge(&v0, &v1, x, y);
            s32 r, g, b;
            u16 c;
            int semi;

            if (w0 + bias0 < 0 || w1 + bias1 < 0 || w2 + bias2 < 0)
                continue;

            if (flags & F_GOURAUD) {
                r = (s32)((w0 * v0.r + w1 * v1.r + w2 * v2.r) / area);
                g = (s32)((w0 * v0.g + w1 * v1.g + w2 * v2.g) / area);
                b = (s32)((w0 * v0.b + w1 * v1.b + w2 * v2.b) / area);
            } else {
                r = v0.r; g = v0.g; b = v0.b;
            }

            if (flags & F_TEXTURED) {
                s32 u = (s32)((w0 * v0.u + w1 * v1.u + w2 * v2.u) / area);
                s32 v = (s32)((w0 * v0.v + w1 * v1.v + w2 * v2.v) / area);
                u16 tx = texel(tex, u, v);
                if (tx == 0)
                    continue;
                c = (flags & F_RAW) ? tx : modulate(tx, r, g, b, x, y, dither);
                semi = (flags & F_SEMI) && (tx & 0x8000);
            } else {
                c = rgb15(r, g, b, x, y, dither);
                semi = (flags & F_SEMI) != 0;
            }
            plot(x, y, c, semi);
        }
    }
}

/* ---- lines ------------------------------------------------------------------------ */

static void draw_line(Vtx a, Vtx b, int flags)
{
    s32 dx, dy, steps, i;
    int dither = gs.dither && (flags & F_GOURAUD);

    a.x += gs.off_x; a.y += gs.off_y;
    b.x += gs.off_x; b.y += gs.off_y;
    dx = b.x - a.x;
    dy = b.y - a.y;
    if (abs(dx) >= 1024 || abs(dy) >= 512)
        return;
    steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

    for (i = 0; i <= steps; i++) {
        s32 x, y, r, g, bl;
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
        plot(x, y, rgb15(r, g, bl, x, y, dither), (flags & F_SEMI) != 0);
    }
}

/* ---- rectangles ------------------------------------------------------------------- */

static void draw_rect(s32 x0, s32 y0, s32 w, s32 h, s32 r, s32 g, s32 b,
                      int flags, const TexState *tex, int u0, int v0)
{
    s32 x, y;
    x0 += gs.off_x;
    y0 += gs.off_y;
    for (y = 0; y < h; y++) {
        s32 py = y0 + y;
        if (py < gs.clip_y0 || py > gs.clip_y1)
            continue;
        for (x = 0; x < w; x++) {
            s32 px = x0 + x;
            u16 c;
            int semi;
            if (px < gs.clip_x0 || px > gs.clip_x1)
                continue;
            if (flags & F_TEXTURED) {
                u16 tx = texel(tex, u0 + x, v0 + y);
                if (tx == 0)
                    continue;
                c = (flags & F_RAW) ? tx : modulate(tx, r, g, b, px, py, 0);
                semi = (flags & F_SEMI) && (tx & 0x8000);
            } else {
                c = rgb15(r, g, b, px, py, 0);
                semi = (flags & F_SEMI) != 0;
            }
            plot(px, py, c, semi);
        }
    }
}

/* ---- VRAM transfers --------------------------------------------------------------- */

void psx_gpu_fill(int x, int y, int w, int h, u32 rgb)
{
    u16 c = rgb15(rgb & 0xFF, (rgb >> 8) & 0xFF, (rgb >> 16) & 0xFF, 0, 0, 0);
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            PIX(x + i, y + j) = c;
}

void psx_gpu_load(int x, int y, int w, int h, const u16 *src)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            u16 c = *src++;
            u16 *p = &PIX(x + i, y + j);
            if (gs.check_mask && (*p & 0x8000))
                continue;
            *p = gs.set_mask ? (u16)(c | 0x8000) : c;
        }
}

void psx_gpu_store(int x, int y, int w, int h, u16 *dst)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            *dst++ = PIX(x + i, y + j);
}

void psx_gpu_move(int sx, int sy, int dx, int dy, int w, int h)
{
    u16 *tmp = (u16 *)malloc((size_t)w * h * sizeof(u16));
    if (!tmp)
        return;
    psx_gpu_store(sx, sy, w, h, tmp);
    psx_gpu_load(dx, dy, w, h, tmp);
    free(tmp);
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
static int cmd_words(const u32 *p, int avail)
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
    TexState ts;

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

    draw_triangle(v[0], v[1], v[2], flags, &ts);
    if (quad)
        draw_triangle(v[1], v[2], v[3], flags, &ts);
}

static void exec_line(const u32 *p, int words)
{
    u32 cmd = p[0] >> 24;
    int gour = (cmd & 0x10) != 0;
    int flags = (gour ? F_GOURAUD : 0) | ((cmd & 2) ? F_SEMI : 0);
    Vtx a, b;
    int k = 0;

    memset(&a, 0, sizeof(a));
    color(p[k++], &a);
    vertex(p[k++], &a);
    while (k < words) {
        if ((p[k] & 0xF000F000u) == 0x50005000u && (cmd & 0x08))
            break;
        b = a;
        if (gour)
            color(p[k++], &b);
        if (k >= words)
            break;
        vertex(p[k++], &b);
        draw_line(a, b, flags);
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
    TexState ts;

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
    draw_rect(v.x, v.y, w, h, v.r, v.g, v.b, flags, &ts, u0, v0);
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
        int n = cmd_words(p, len);
        if (n > len)
            n = len;
        exec_command(p, n);
        p += n;
        len -= n;
    }
}

void psx_gpu_reset(void)
{
    memset(&gs, 0, sizeof(gs));
    gs.clip_x1 = VRAM_W - 1;
    gs.clip_y1 = VRAM_H - 1;
}
