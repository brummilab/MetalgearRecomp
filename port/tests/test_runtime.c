/*
 * Self-tests of the PlayStation runtime that need no game data:
 * GTE perspective transformation and GPU rasterization.
 *
 *   test_runtime [out.ppm]   writes the test picture if a path is given
 */
#include "psx/psx.h"

#include <stdio.h>
#include <string.h>

unsigned int psx_gte_read_data(int reg);
void         psx_gte_write_data(int reg, unsigned int value);
unsigned int psx_gte_read_ctrl(int reg);
void         psx_gte_write_ctrl(int reg, unsigned int value);
void         psx_gte_command(unsigned int cmd);
void         psx_gpu_exec(const uint32_t *p, int len);
void         psx_gpu_reset(void);
extern uint16_t psx_vram[];

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
void plat_present(const uint16_t *v, int x, int y, int w, int h, int r, int e) { (void)v; (void)x; (void)y; (void)w; (void)h; (void)r; (void)e; }
int  plat_pump_events(void) { return 1; }
void plat_audio_queue(const int16_t *s, int f) { (void)s; (void)f; }
int  plat_audio_queued_frames(void) { return 0; }
void psx_log(const char *fmt, ...) { (void)fmt; }

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
    if (argc > 1)
        write_ppm(argv[1]);
    printf("%s (%d failure%s)\n", failures ? "FAILED" : "all tests passed", failures, failures == 1 ? "" : "s");
    return failures != 0;
}
