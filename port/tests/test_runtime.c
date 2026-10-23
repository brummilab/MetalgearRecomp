/*
 * Self-tests of the PlayStation runtime that need no game data:
 * GTE perspective transformation and GPU rasterization.
 *
 *   test_runtime [out.ppm]   writes the test picture if a path is given
 */
#include "psx/psx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned int psx_gte_read_data(int reg);
void         psx_gte_write_data(int reg, unsigned int value);
unsigned int psx_gte_read_ctrl(int reg);
void         psx_gte_write_ctrl(int reg, unsigned int value);
void         psx_gte_command(unsigned int cmd);
void         psx_gpu_exec(const uint32_t *p, int len);
void         psx_gpu_reset(void);
int          psx_gpu_set_scale(int scale);
const uint16_t *psx_gpu_display(int *stride, int *shift);
extern int   psx_gte_widescreen;
void         psx_ws_init(int widescreen, int fit_hud);
void         psx_interp_init(int enable);
void         psx_gpu_vblank(void);
DISPENV     *PutDispEnv(DISPENV *env);
long RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, long *sxy0, long *sxy1, long *sxy2, long *p, long *flag);
extern uint16_t *psx_vram;

long RotTransPers(SVECTOR *v0, long *sxy, long *p, long *flag);
void SetRotMatrix(MATRIX *m);
void SetTransMatrix(MATRIX *m);
MATRIX *RotMatrix(SVECTOR *r, MATRIX *m);
int  rsin(int a);
int  rcos(int a);
long ratan2(long y, long x);
long SquareRoot0(long a);
u_long *ClearOTagR(u_long *ot, int n);
void DrawOTag(u_long *p);

/* the runtime's platform hooks, stubbed for the tests */
uint16_t plat_pad_buttons(int port) { (void)port; return 0; }
uint64_t plat_ticks_us(void) { return 0; }
void plat_sleep_us(uint64_t us) { (void)us; }
void plat_present(const uint16_t *v, int st, int sh, int x, int y, int w, int h, int r, int e, int pb) { (void)v; (void)st; (void)sh; (void)x; (void)y; (void)w; (void)h; (void)r; (void)e; (void)pb; }
int  plat_pump_events(void) { return 1; }
/* audio output: peak level of everything mixed since the last reset */
static int audio_peak;
void plat_audio_queue(const int16_t *s, int f)
{
    int i;
    for (i = 0; i < f * 2; i++)
        if (abs(s[i]) > audio_peak)
            audio_peak = abs(s[i]);
}
int  plat_audio_queued_frames(void) { return 0; }
void psx_log(const char *fmt, ...) { (void)fmt; }
void psx_fatal(const char *fmt, ...) { (void)fmt; }

/* fake time for the CD stream: every psx_idle() is one vblank */
static uint32_t fake_vblank;
uint32_t psx_vblank_count(void) { return fake_vblank; }
void psx_cd_interrupt(void);
void psx_idle(void) { fake_vblank++; psx_cd_interrupt(); }

int psx_spu_xa_pending(void);
void psx_spu_init(void);
void psx_spu_vblank(void);

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_gte(void)
{
    MATRIX m;
    SVECTOR rot = { 0, 0, 0, 0 }, v = { 100, 50, 0, 0 };
    long sxy, p, flag, otz;

    RotMatrix(&rot, &m);
    m.t[0] = 0; m.t[1] = 0; m.t[2] = 1000;
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    psx_gte_write_ctrl(24, 160 << 16);   /* OFX */
    psx_gte_write_ctrl(25, 120 << 16);   /* OFY */
    psx_gte_write_ctrl(26, 1000);        /* H */

    otz = RotTransPers(&v, &sxy, &p, &flag);
    CHECK((short)sxy == 260 && (short)(sxy >> 16) == 170, "RTPS screen xy = %d,%d (want 260,170)", (short)sxy, (short)(sxy >> 16));
    CHECK(otz == 250, "RTPS otz = %ld (want 250)", otz);
    CHECK((flag & 0x80000000) == 0, "RTPS flag = %08lx (want no error)", flag);

    psx_gte_widescreen = 1;
    RotTransPers(&v, &sxy, &p, &flag);
    CHECK((short)sxy == 235 && (short)(sxy >> 16) == 170, "widescreen RTPS = %d,%d (want 235,170)", (short)sxy, (short)(sxy >> 16));
    psx_gte_widescreen = 0;

    /* point at twice the distance: half the offset from the centre */
    m.t[2] = 2000;
    SetTransMatrix(&m);
    RotTransPers(&v, &sxy, &p, &flag);
    CHECK((short)sxy == 210 && (short)(sxy >> 16) == 145, "RTPS far = %d,%d (want 210,145)", (short)sxy, (short)(sxy >> 16));

    /* NCLIP of a clockwise and a counter-clockwise triangle */
    psx_gte_write_data(12, 0 | (0 << 16));
    psx_gte_write_data(13, 10 | (0 << 16));
    psx_gte_write_data(14, 0 | (10 << 16));
    psx_gte_command(0x1400006);
    CHECK((int)psx_gte_read_data(24) == 100, "NCLIP = %d (want 100)", (int)psx_gte_read_data(24));

    CHECK(rsin(1024) == 4096 && rcos(0) == 4096 && rsin(0) == 0, "rsin/rcos");
    CHECK(ratan2(1, 0) == 1024 && ratan2(0, -1) == 2048, "ratan2 = %ld %ld", ratan2(1, 0), ratan2(0, -1));
    CHECK(SquareRoot0(1000000) == 1000, "SquareRoot0");
}

static uint16_t px(int x, int y) { return psx_vram[y * 1024 + x]; }

static void test_gpu(void)
{
    static u_long ot[4];
    static uint32_t tri[1 + 4], sprt[1 + 4], g3[1 + 6];
    uint32_t env[] = {
        0xE3000000u,                              /* area 0,0 */
        0xE4000000u | 319 | (239 << 10),          /* .. 319,239 */
        0xE5000000u,                              /* offset 0,0 */
        0x02202020u, 0, 320 | (240 << 16),        /* clear to grey */
    };
    uint32_t clut_row[16];
    int i;

    psx_gpu_reset();
    psx_gpu_exec(env, 6);
    CHECK(px(5, 5) == (4 | (4 << 5) | (4 << 10)), "fill colour %04x", px(5, 5));

    /* red flat triangle through the ordering table */
    tri[0] = (4u << 24);
    tri[1] = 0x200000FFu;
    tri[2] = 10 | (10 << 16);
    tri[3] = 110 | (10 << 16);
    tri[4] = 10 | (110 << 16);
    /* gouraud triangle */
    g3[0] = (6u << 24);
    g3[1] = 0x300000FFu; g3[2] = 150 | (10 << 16);
    g3[3] = 0x0000FF00u; g3[4] = 300 | (10 << 16);
    g3[5] = 0x00FF0000u; g3[6] = 150 | (200 << 16);
    ClearOTagR(ot, 4);
    tri[0] |= ot[1] & 0xFFFFFF; ot[1] = (u_long)((uintptr_t)tri & 0xFFFFFF);
    g3[0] |= ot[2] & 0xFFFFFF;  ot[2] = (u_long)((uintptr_t)g3 & 0xFFFFFF);
    DrawOTag(&ot[3]);

    CHECK(px(20, 20) == 0x1F, "flat triangle inside = %04x", px(20, 20));
    CHECK(px(100, 100) != 0x1F, "flat triangle outside = %04x", px(100, 100));
    CHECK(px(10, 10) == 0x1F && px(110, 10) != 0x1F, "top-left fill rule (%04x %04x)", px(10, 10), px(110, 10));
    CHECK((px(152, 12) & 0x1F) > 25, "gouraud red corner = %04x", px(152, 12));

    /* 4-bit textured sprite: CLUT at (0,480), texture page at (640,0) */
    for (i = 0; i < 16; i++)
        clut_row[i] = 0;
    for (i = 0; i < 16; i++)
        psx_vram[480 * 1024 + i] = (uint16_t)(i == 0 ? 0 : (i << 1) | 0x8000);
    for (i = 0; i < 16; i++)
        psx_vram[i * 1024 + 640] = 0x4321; /* texels 1,2,3,4 */
    {
        uint32_t page[] = { 0xE100000Au }; /* tpage x=640, 4 bit */
        psx_gpu_exec(page, 1);
    }
    sprt[0] = 4u << 24 | 0xFFFFFF;
    sprt[1] = 0x65808080u;                         /* raw textured sprite */
    sprt[2] = 200 | (200 << 16);
    sprt[3] = 0 | (0 << 8) | ((480u << 6) << 16);  /* uv 0,0, clut y=480 */
    sprt[4] = 4 | (4 << 16);
    psx_gpu_exec(&sprt[1], 4);
    CHECK((px(200, 200) & 0x7FFF) == 2 && (px(203, 200) & 0x7FFF) == 8, "4-bit sprite texels %04x %04x", px(200, 200), px(203, 200));
    (void)clut_row;
}

/* ---- movies: CD streaming -> VLC -> MDEC -------------------------------------------- */

void psx_cd_init(const char *path);
int  CdControl(u_char com, u_char *param, u_char *result);
int  CdRead2(long mode);
u_long StGetNext(u_long **addr, u_long **header);
u_long StFreeRing(u_long *base);
void StSetStream(u_long mode, u_long start, u_long end, void (*f1)(void), void (*f2)(void));
int  DecDCTvlc2(u_long *bs, u_long *buf, u_short *table);
void DecDCTReset(int mode);
void DecDCTin(u_long *buf, int mode);
void DecDCTout(u_long *buf, int size);
int  DecDCToutCallback(void (*func)(void));

typedef struct { uint8_t buf[4096]; int bits; } BitWriter;

static void put_bits(BitWriter *w, unsigned v, int n)
{
    while (n--) {
        int word = w->bits >> 4, bit = 15 - (w->bits & 15);
        if ((v >> n) & 1)
            w->buf[word * 2 + (bit >= 8)] |= (uint8_t)(1 << (bit & 7));
        w->bits++;
    }
}

/* DC-only block, optionally one AC coefficient, then end of block */
static void put_block(BitWriter *w, int dc, int with_ac)
{
    put_bits(w, (unsigned)dc & 0x3FF, 10);
    if (with_ac == 1)
        put_bits(w, 0x3, 3), put_bits(w, 0, 1);            /* "011" + sign: run 1, level 1 */
    if (with_ac == 2)
        put_bits(w, 1, 6), put_bits(w, (5 << 10) | 3, 16); /* escape: run 5, level 3 */
    put_bits(w, 2, 2);                                      /* "10" */
}

static uint16_t movie_out[2][16 * 16];
static int      movie_strips;

static void movie_cb(void)
{
    movie_strips++;
    if (movie_strips < 2)
        DecDCTout((u_long *)movie_out[movie_strips], 16 * 16 / 2);
}

static void test_movie(void)
{
    static uint8_t image[4][2352];
    static uint32_t dctin[0x17800 / 4];
    static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    BitWriter w;
    uint8_t *d;
    u_long *addr, *hdr;
    uint8_t loc[4] = { 0x00, 0x02, 0x00, 0 }; /* LBA 0 */
    char path[] = "test_movie.bin";
    FILE *f;
    int i, b, tries;

    /* 32x16 frame = two macroblocks: grey 200 (with an AC code) and grey 28 (with an escape) */
    memset(&w, 0, sizeof(w));
    for (b = 0; b < 6; b++)
        put_block(&w, b < 2 ? 0 : 288, b == 5 ? 1 : 0);
    for (b = 0; b < 6; b++)
        put_block(&w, b < 2 ? 0 : -400, b == 5 ? 2 : 0);

    memset(image, 0, sizeof(image));
    for (i = 0; i < 3; i++) {
        memcpy(image[i], sync, 12);
        image[i][15] = 2;
    }
    /* sectors 0 and 1: the frame's two STR sectors */
    for (i = 0; i < 2; i++) {
        d = &image[i][24];
        image[i][18] = image[i][22] = 0x08;           /* submode: data */
        d[0] = 0x60; d[1] = 0x01;                     /* STR magic 0x0160 */
        d[2] = 0x01; d[3] = 0x80;                     /* type 0x8001 */
        d[4] = (uint8_t)i; d[6] = 2;                  /* sector i of 2 */
        d[8] = 1;                                     /* frame 1 */
        d[16] = 32; d[18] = 16;                       /* 32x16 */
        if (i == 0) {
            /* frame payload: 8-byte bitstream header, then the bitstream */
            int halfwords = 2 * 6 * 2 + 2;
            d[32] = (uint8_t)((halfwords + 1) / 2); d[34] = 0x00; d[35] = 0x38;
            d[36] = 1;                                /* qscale */
            d[38] = 2;                                /* version 2 */
            memcpy(d + 40, w.buf, 2000);
        }
    }
    /* sector 2: XA audio, stereo 37.8 kHz 4 bit (silence) */
    image[2][18] = image[2][22] = 0x64;
    image[2][19] = image[2][23] = 0x01;

    f = fopen(path, "wb");
    fwrite(image, 1, sizeof(image), f);
    fclose(f);
    psx_cd_init(path);

    StSetStream(0, 0, 0xFFFFFFFF, NULL, NULL);
    CdControl(0x02, loc, NULL);               /* CdlSetloc */
    CdRead2(0x120 | 0x80 | 0x40);

    for (tries = 0; tries < 100 && StGetNext(&addr, &hdr) != 0; tries++)
        ;
    CHECK(tries < 100, "no STR frame delivered");
    if (tries >= 100)
        return;
    CHECK(((uint16_t *)hdr)[1] == 0x8001 && ((uint16_t *)hdr)[8] == 32, "STR header type %04x width %d",
          ((uint16_t *)hdr)[1], ((uint16_t *)hdr)[8]);
    CHECK(tries >= 1, "stream delivered faster than the drive (%d)", tries);

    DecDCTvlc2(addr, (u_long *)dctin, NULL);
    StFreeRing(addr);
    CHECK(dctin[0] == 2 * 6 * 2 + 2, "VLC produced %u halfwords (want 26)", dctin[0]);
    if (getenv("MDEC_DUMP")) { unsigned k; for (k = 1; k <= dctin[0] / 2 + 1; k++) printf("%08x ", dctin[k]); printf("\n"); }

    DecDCTReset(0);
    DecDCToutCallback(movie_cb);
    movie_strips = 0;
    DecDCTout((u_long *)movie_out[0], 16 * 16 / 2);
    DecDCTin((u_long *)dctin, 2);
    CHECK(movie_strips == 2, "MDEC delivered %d strips (want 2)", movie_strips);
    CHECK(movie_out[0][0] == (25 | (25 << 5) | (25 << 10)), "macroblock 0 pixel %04x (want grey 200)", movie_out[0][0]);
    CHECK(movie_out[1][0] == (3 | (3 << 5) | (3 << 10)), "macroblock 1 pixel %04x (want grey 28)", movie_out[1][0]);

    for (tries = 0; tries < 10; tries++)
        psx_idle();
    /* one stereo 37.8 kHz sector = 2016 frames = 2352 frames at 44.1 kHz */
    CHECK(abs(psx_spu_xa_pending() - 2352) <= 2, "XA queued %d frames (want ~2352)", psx_spu_xa_pending());
    remove(path);
}

/* ---- SPU reverb ---------------------------------------------------------------------- */

typedef struct { short left, right; } SpuVol;
typedef struct {
    u_long voice, mask; SpuVol volume, volmode, volumex;
    u_short pitch, note, sample_note; short envx;
    u_long addr, loop_addr; long a_mode, s_mode, r_mode;
    u_short ar, dr, sr, rr, sl, adsr1, adsr2;
} VoiceAttr;
typedef struct { u_long mask; long mode; SpuVol depth; long delay, feedback; } RevAttr;
typedef struct { SpuVol volume; long reverb, mix; } ExtAttr;
typedef struct { u_long mask; SpuVol mvol, mvolmode, mvolx; ExtAttr cd, ext; } CommonAttr;

unsigned long SpuSetTransferStartAddr(unsigned long addr);
unsigned long SpuWrite(unsigned char *addr, unsigned long size);
void SpuSetVoiceAttr(VoiceAttr *a);
void SpuSetKey(long on_off, unsigned long voice_bit);
void SpuSetCommonAttr(CommonAttr *a);
long SpuSetReverb(long on_off);
long SpuSetReverbModeParam(RevAttr *attr);
long SpuSetReverbDepth(RevAttr *attr);
long SpuReserveReverbWorkArea(long on_off);
long SpuClearReverbWorkArea(long mode);
unsigned long SpuSetReverbVoice(long on_off, unsigned long voice_bit);

/* plays a 28-sample impulse; returns the peak level 1/6 to 1/2 s later */
static int reverb_tail(int with_reverb)
{
    static unsigned char block[16];
    VoiceAttr v;
    RevAttr r;
    CommonAttr c;
    int i, tail = 0;

    psx_spu_init();
    memset(block, 0x77, sizeof(block));
    block[0] = 0x00;     /* shift 0, filter 0 */
    block[1] = 0x01;     /* end, no repeat: the voice stops after one block */
    SpuSetTransferStartAddr(0x1010);
    SpuWrite(block, 16);

    memset(&c, 0, sizeof(c));
    c.mask = 3;
    c.mvol.left = c.mvol.right = 0x3FFF;
    SpuSetCommonAttr(&c);

    SpuReserveReverbWorkArea(1);
    SpuClearReverbWorkArea(4);
    memset(&r, 0, sizeof(r));
    r.mask = 1;
    r.mode = 4;          /* STUDIO_C, as MGS uses */
    CHECK(SpuSetReverbModeParam(&r) == 0, "reverb mode rejected");
    r.mask = 6;
    r.depth.left = r.depth.right = 0x4000;
    SpuSetReverbDepth(&r);
    SpuSetReverb(1);
    SpuSetReverbVoice(0, 0xFFFFFF);
    if (with_reverb)
        SpuSetReverbVoice(1, 1);

    memset(&v, 0, sizeof(v));
    v.voice = 1;
    v.mask = (1 << 0) | (1 << 1) | (1 << 4) | (1 << 7) | (1 << 17) | (1 << 18);
    v.volume.left = v.volume.right = 0x3FFF;
    v.pitch = 0x1000;
    v.addr = 0x1010;
    v.adsr1 = 0x000F;    /* fastest attack, full sustain */
    v.adsr2 = 0x0000;
    SpuSetVoiceAttr(&v);
    SpuSetKey(1, 1);

    for (i = 0; i < 30; i++) {
        audio_peak = 0;
        psx_spu_vblank();
        if (i >= 10 && audio_peak > tail)
            tail = audio_peak;
    }
    SpuSetReverb(0);
    return tail;
}

static void test_reverb(void)
{
    int dry = reverb_tail(0), wet = reverb_tail(1);
    CHECK(dry == 0, "without reverb the impulse should be over (peak %d)", dry);
    CHECK(wet > 50, "reverb tail missing (peak %d)", wet);
}

/* ---- SPU interrupt as a clock (MGS's streamed voice) ------------------------------------
 * The sound driver loops a silent "timer" voice over 512 bytes and moves the
 * IRQ address between both halves in its handler; every IRQ advances its
 * estimate of the stream position. Each crossing must give one handler call:
 * 256 bytes = 16 blocks = 448 samples, at 44.1 kHz about 98 per second. */
typedef void (*SpuIRQCallbackProc)(void);
long SpuSetIRQ(long on_off);
unsigned long SpuSetIRQAddr(unsigned long addr);
SpuIRQCallbackProc SpuSetIRQCallback(SpuIRQCallbackProc func);
void psx_spu_interrupt(void);

static int irq_count;

static void timer_irq(void)
{
    irq_count++;
    SpuSetIRQAddr((irq_count & 1) ? 0x1010 : 0x1110);
    SpuSetIRQ(3);        /* SPU_RESET */
}

static void test_spu_irq(void)
{
    static unsigned char blank[512];
    VoiceAttr v;
    int i;

    psx_spu_init();
    memset(blank, 0, sizeof(blank));
    for (i = 0; i < 32; i++)
        blank[i * 16 + 1] = i == 0 ? 0x04 : i == 31 ? 0x03 : 0x00;   /* loop start / end+repeat */
    SpuSetTransferStartAddr(0x1010);
    SpuWrite(blank, sizeof(blank));

    memset(&v, 0, sizeof(v));
    v.voice = 1;
    v.mask = (1 << 4) | (1 << 7) | (1 << 17) | (1 << 18);
    v.pitch = 0x1000;
    v.addr = 0x1010;
    v.adsr1 = 0x000F;
    SpuSetVoiceAttr(&v);
    SpuSetKey(1, 1);

    irq_count = 0;
    SpuSetIRQCallback(timer_irq);
    SpuSetIRQAddr(0x1110);
    SpuSetIRQ(1);
    for (i = 0; i < 60; i++) {
        psx_spu_vblank();
        psx_spu_interrupt();
    }
    SpuSetIRQ(0);
    SpuSetIRQCallback(NULL);
    SpuSetKey(0, 1);
    /* 60 vblanks = 60 * 44100 / 59.94 frames = 44144 -> 98.5 crossings */
    CHECK(irq_count >= 96 && irq_count <= 100, "SPU IRQ clock: %d handler calls in 1 s (want ~98)", irq_count);
}

/* ---- widescreen HUD ------------------------------------------------------------------- */

static void test_widescreen_hud(void)
{
    static u_long ot[4];
    static uint32_t tri[1 + 4], hud_poly[1 + 5], hud_tile[1 + 3];
    uint32_t env[] = {
        0xE3000000u, 0xE4000000u | 319 | (239 << 10), 0xE5000000u,
        0x02000000u, 0, 320 | (240 << 16),          /* clear to black */
    };
    SVECTOR a = { -50, -50, 0, 0 }, b = { 50, -50, 0, 0 }, c = { -50, 50, 0, 0 }, rot = { 0, 0, 0, 0 };
    MATRIX m;
    long s0, s1, s2, p, flag;

    psx_ws_init(1, 1);
    RotMatrix(&rot, &m);
    m.t[0] = 0; m.t[1] = 0; m.t[2] = 1000;
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    psx_gte_write_ctrl(24, 160 << 16);
    psx_gte_write_ctrl(25, 120 << 16);
    psx_gte_write_ctrl(26, 1000);
    RotTransPers3(&a, &b, &c, &s0, &s1, &s2, &p, &flag);
    CHECK((short)s1 >= 196 && (short)s1 <= 198, "3D vertex squeezed by the GTE: x = %d (want ~197)", (short)s1);

    psx_gpu_reset();
    psx_gpu_exec(env, 6);
    /* green 3D triangle from the projected coordinates */
    tri[0] = 4u << 24; tri[1] = 0x2000FF00u;
    tri[2] = (uint32_t)s0; tri[3] = (uint32_t)s1; tri[4] = (uint32_t)s2;
    /* blue HUD quad at fixed coordinates x 0..40, y 200..220 */
    hud_poly[0] = 5u << 24; hud_poly[1] = 0x28FF0000u;
    hud_poly[2] = 0 | (200 << 16); hud_poly[3] = 40 | (200 << 16);
    hud_poly[4] = 0 | (220 << 16); hud_poly[5] = 40 | (220 << 16);
    /* red HUD tile at x 280..320, y 0..20 */
    hud_tile[0] = 3u << 24; hud_tile[1] = 0x600000FFu;
    hud_tile[2] = 280 | (0 << 16); hud_tile[3] = 40 | (20 << 16);
    ClearOTagR(ot, 4);
    tri[0] |= ot[1] & 0xFFFFFF;      ot[1] = (u_long)((uintptr_t)tri & 0xFFFFFF);
    hud_poly[0] |= ot[2] & 0xFFFFFF; ot[2] = (u_long)((uintptr_t)hud_poly & 0xFFFFFF);
    hud_tile[0] |= ot[3] & 0xFFFFFF; ot[3] = (u_long)((uintptr_t)hud_tile & 0xFFFFFF);
    DrawOTag(&ot[3]);

    /* 3D: drawn where the GTE put it, not squeezed a second time */
    CHECK(px(130, 80) == (31 << 5), "3D triangle inside = %04x", px(130, 80));
    CHECK(px(190, 75) == (31 << 5), "3D triangle near its right corner = %04x (squeezed twice?)", px(190, 75));
    /* HUD quad 0..40 -> 40..70 around the centre 160 */
    CHECK(px(10, 210) == 0, "HUD quad not squeezed (pixel at x=10 is %04x)", px(10, 210));
    CHECK(px(50, 210) == (31 << 10), "HUD quad missing at x=50 (%04x)", px(50, 210));
    CHECK(px(75, 210) == 0, "HUD quad too wide (pixel at x=75 is %04x)", px(75, 210));
    /* HUD tile 280..320 -> 250..280 */
    CHECK(px(255, 10) == 31 && px(290, 10) == 0, "HUD tile squeeze (%04x at 255, %04x at 290)", px(255, 10), px(290, 10));

    psx_ws_init(0, 0);
}

/* ---- 60 fps interpolation ------------------------------------------------------------ */

/* one game frame: a 20x20 world square at x = wx, drawn into area_x; returns the OT */
static u_long *interp_frame(int wx, int area_x, u_long *ot, uint32_t *pkt)
{
    SVECTOR a = { 0, 0, 0, 0 }, b = { 20, 0, 0, 0 }, c = { 0, 20, 0, 0 }, rot = { 0, 0, 0, 0 };
    static MATRIX m;   /* one object: same matrix address every frame */
    long s0, s1, s2, p, flag;
    uint32_t *env = pkt, *tri = pkt + 8;

    RotMatrix(&rot, &m);
    m.t[0] = wx; m.t[1] = 0; m.t[2] = 1000;
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    RotTransPers3(&a, &b, &c, &s0, &s1, &s2, &p, &flag);

    env[0] = 6u << 24;
    env[1] = 0xE3000000u | area_x;
    env[2] = 0xE4000000u | (area_x + 319) | (239 << 10);
    env[3] = 0xE5000000u | (area_x & 0x7FF);
    env[4] = 0x02000000u; env[5] = area_x; env[6] = 320 | (240 << 16);
    tri[0] = 4u << 24; tri[1] = 0x200000FFu;
    tri[2] = (uint32_t)s0; tri[3] = (uint32_t)s1; tri[4] = (uint32_t)s2;
    ClearOTagR(ot, 3);
    tri[0] |= ot[1] & 0xFFFFFF; ot[1] = (u_long)((uintptr_t)tri & 0xFFFFFF);
    env[0] |= ot[2] & 0xFFFFFF; ot[2] = (u_long)((uintptr_t)env & 0xFFFFFF);
    return &ot[2];
}

/* leftmost red pixel in row y of the area at area_x */
static int red_left(int area_x, int y)
{
    int x;
    for (x = 0; x < 320; x++)
        if (px(area_x + x, y) == 0x1F)
            return x;
    return -1;
}

static void test_interp(void)
{
    static u_long ot[2][3];
    static uint32_t pkt[2][16];
    DISPENV disp;
    int y = 125;

    psx_interp_init(1);
    psx_gpu_reset();
    psx_gte_write_ctrl(24, 160 << 16);
    psx_gte_write_ctrl(25, 120 << 16);
    psx_gte_write_ctrl(26, 1000);
    memset(&disp, 0, sizeof(disp));
    disp.disp.w = 320; disp.disp.h = 240;

    /* displayed buffer = drawing buffer: interpolated picture first */
    PutDispEnv(&disp);
    fake_vblank = 100;
    DrawOTag(interp_frame(0, 0, ot[0], pkt[0]));
    CHECK(red_left(0, y) == 160, "frame 1 at x=%d (want 160)", red_left(0, y));
    fake_vblank += 2;
    DrawOTag(interp_frame(20, 0, ot[1], pkt[1]));
    CHECK(red_left(0, y) == 170, "in-between picture at x=%d (want 170)", red_left(0, y));
    fake_vblank++;
    psx_gpu_vblank();
    CHECK(red_left(0, y) == 180, "real frame 2 at x=%d (want 180)", red_left(0, y));

    /* back buffer style: frame drawn at x=320 while x=0 is on screen */
    fake_vblank += 1;
    DrawOTag(interp_frame(40, 320, ot[0], pkt[0]));
    CHECK(red_left(320, y) == 200, "back buffer frame at x=%d (want 200)", red_left(320, y));
    CHECK(red_left(0, y) == 180, "screen must still show frame 2 (x=%d)", red_left(0, y));
    fake_vblank++;
    psx_gpu_vblank();
    CHECK(red_left(0, y) == 190, "in-between picture on screen at x=%d (want 190)", red_left(0, y));

    /* camera cut (100 px jump): the in-between picture is not blended */
    disp.disp.x = 320;
    PutDispEnv(&disp);
    fake_vblank += 1;
    DrawOTag(interp_frame(140, 0, ot[1], pkt[1]));
    fake_vblank++;
    psx_gpu_vblank();
    CHECK(red_left(320, y) == 300, "after a cut: x=%d (want 300, unblended)", red_left(320, y));

    psx_interp_init(0);
}

/* ---- upscaled rendering ---------------------------------------------------------------- */

static void test_hires(int scale)
{
    int stride, shift, x, y, mism = 0;
    const uint16_t *hv;

    CHECK(psx_gpu_set_scale(scale) == scale, "render scale %d not available", scale);
    test_gpu();                       /* same scene; native results are checked there */
    hv = psx_gpu_display(&stride, &shift);
    CHECK(shift == (scale == 4 ? 2 : 1), "display shift %d", shift);
    /* every upscaled block must agree with its native pixel away from edges */
    for (y = 1; y < 239; y++)
        for (x = 1; x < 319; x++) {
            uint16_t n = px(x, y);
            if (px(x - 1, y) != n || px(x + 1, y) != n || px(x, y - 1) != n || px(x, y + 1) != n)
                continue;    /* edge or gradient: upscaling may differ */
            if ((hv[((y << shift) + (1 << shift) / 2) * stride + (x << shift) + (1 << shift) / 2] & 0x7FFF) != (n & 0x7FFF))
                mism++;
        }
    CHECK(mism == 0, "scale %d: %d upscaled pixels differ from the original", scale, mism);
    /* textured sprite: texels come through at full resolution */
    CHECK((hv[(200 << shift) * stride + (203 << shift)] & 0x7FFF) == 8, "scale %d: sprite texel %04x", scale,
          hv[(200 << shift) * stride + (203 << shift)]);
}

static void write_hires_ppm(const char *path)
{
    int stride, shift, x, y;
    const uint16_t *hv = psx_gpu_display(&stride, &shift);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", 320 << shift, 240 << shift);
    for (y = 0; y < (240 << shift); y++)
        for (x = 0; x < (320 << shift); x++) {
            uint16_t c = hv[y * stride + x];
            unsigned char rgb[3] = { (unsigned char)((c & 31) << 3), (unsigned char)(((c >> 5) & 31) << 3), (unsigned char)(((c >> 10) & 31) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int x, y;
    if (!f)
        return;
    fprintf(f, "P6\n320 240\n255\n");
    for (y = 0; y < 240; y++)
        for (x = 0; x < 320; x++) {
            uint16_t c = px(x, y);
            unsigned char rgb[3] = { (unsigned char)((c & 31) << 3), (unsigned char)(((c >> 5) & 31) << 3), (unsigned char)(((c >> 10) & 31) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

int main(int argc, char **argv)
{
    test_gte();
    test_gpu();
    test_movie();
    test_reverb();
    test_spu_irq();
    test_widescreen_hud();
    test_interp();
    if (argc > 1)
        write_ppm(argv[1]);
    test_hires(2);
    test_hires(4);
    if (argc > 2)
        write_hires_ppm(argv[2]);
    /* widescreen HUD at 2x: squeezed in the upscaled raster as well */
    psx_gpu_set_scale(2);
    test_widescreen_hud();
    {
        int stride, shift;
        const uint16_t *hv = psx_gpu_display(&stride, &shift);
        CHECK(hv[(421) * stride + 101] == (31 << 10) && hv[421 * stride + 21] == 0,
              "2x widescreen HUD quad (%04x at x=101, %04x at x=21)", hv[421 * stride + 101], hv[421 * stride + 21]);
    }
    if (argc > 3)
        write_hires_ppm(argv[3]);
    psx_gpu_set_scale(1);
    printf("%s (%d failure%s)\n", failures ? "FAILED" : "all tests passed", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
