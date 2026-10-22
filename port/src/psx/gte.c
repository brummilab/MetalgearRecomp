/*
 * GTE (Geometry Transformation Engine, COP2) emulation.
 *
 * Follows the hardware behaviour documented in psx-spx (saturation, flags,
 * the UNR division used by RTPS/RTPT), so translated game code produces the
 * same screen coordinates, depths and colours as on the console.
 */
#include "psx.h"

typedef int64_t  s64;
typedef int32_t  s32;
typedef int16_t  s16;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

int psx_gte_widescreen;

static u32 D[32]; /* data registers */
static u32 C[32]; /* control registers */

/* --- register helpers ----------------------------------------------------- */

#define VX(n) ((s16)(D[(n) * 2] & 0xFFFF))
#define VY(n) ((s16)(D[(n) * 2] >> 16))
#define VZ(n) ((s16)(D[(n) * 2 + 1] & 0xFFFF))

#define RGBC  D[6]
#define OTZ   D[7]
#define IR0   D[8]
#define IR1   D[9]
#define IR2   D[10]
#define IR3   D[11]
#define SXY0  D[12]
#define SXY1  D[13]
#define SXY2  D[14]
#define SZ0   D[16]
#define SZ1   D[17]
#define SZ2   D[18]
#define SZ3   D[19]
#define RGB0  D[20]
#define RGB1  D[21]
#define RGB2  D[22]
#define MAC0  D[24]
#define MAC1  D[25]
#define MAC2  D[26]
#define MAC3  D[27]

/* matrix element (mat 0 = rotation, 1 = light, 2 = light colour), row r, col c */
static s16 mat(int m, int r, int c)
{
    int idx = r * 3 + c;            /* 0..8 */
    u32 w = C[m * 8 + idx / 2];
    return (idx & 1) ? (s16)(w >> 16) : (s16)(w & 0xFFFF);
}

#define TRX ((s32)C[5])
#define TRY ((s32)C[6])
#define TRZ ((s32)C[7])
#define RBK ((s32)C[13])
#define GBK ((s32)C[14])
#define BBK ((s32)C[15])
#define RFC ((s32)C[21])
#define GFC ((s32)C[22])
#define BFC ((s32)C[23])
#define OFX ((s32)C[24])
#define OFY ((s32)C[25])
#define H   ((u16)C[26])
#define DQA ((s16)C[27])
#define DQB ((s32)C[28])
#define ZSF3 ((s16)C[29])
#define ZSF4 ((s16)C[30])
#define FLAG C[31]

/* --- flags & saturation ----------------------------------------------------- */

static void mac_check(int n, s64 v)
{
    /* MAC1..3: 44-bit overflow; flags 30..25 */
    if (v > 0x7FFFFFFFFFFLL)
        FLAG |= 1u << (31 - n);
    else if (v < -0x80000000000LL)
        FLAG |= 1u << (28 - n);
}

static s64 mac_ext(int n, s64 v)
{
    mac_check(n, v);
    /* sign-extend to 44 bits */
    return (v << 20) >> 20;
}

static s32 set_mac(int n, s64 v, int shift)
{
    s32 r;
    v = mac_ext(n, v);
    r = (s32)(v >> shift);
    D[24 + n] = (u32)r;
    return r;
}

static s32 lim_ir(int n, s32 v, int lm)
{
    s32 lo = lm ? 0 : -0x8000;
    if (v < lo) {
        FLAG |= 1u << (25 - n);
        v = lo;
    } else if (v > 0x7FFF) {
        FLAG |= 1u << (25 - n);
        v = 0x7FFF;
    }
    D[8 + n] = (u32)v;
    return v;
}

/* IR3 when sf = 0 in RTPS: flag uses the >>12 value but saturation uses raw */
static void lim_ir3_rtp(s64 mac3_raw, int sf, int lm)
{
    s32 v = (s32)(mac3_raw >> (sf ? 12 : 0));
    s32 f = (s32)(mac3_raw >> 12);
    s32 lo = lm ? 0 : -0x8000;
    if (f < -0x8000 || f > 0x7FFF)
        FLAG |= 1u << 22;
    if (v < lo) v = lo;
    if (v > 0x7FFF) v = 0x7FFF;
    IR3 = (u32)v;
}

static u8 lim_color(int n, s32 v)
{
    if (v < 0) {
        FLAG |= 1u << (21 - n);
        return 0;
    }
    if (v > 0xFF) {
        FLAG |= 1u << (21 - n);
        return 0xFF;
    }
    return (u8)v;
}

static u16 lim_sz3(s64 v)
{
    if (v < 0) {
        FLAG |= 1u << 18;
        return 0;
    }
    if (v > 0xFFFF) {
        FLAG |= 1u << 18;
        return 0xFFFF;
    }
    return (u16)v;
}

static s16 lim_sxy(int n, s32 v)
{
    if (v < -0x400) {
        FLAG |= 1u << (14 - n);
        return -0x400;
    }
    if (v > 0x3FF) {
        FLAG |= 1u << (14 - n);
        return 0x3FF;
    }
    return (s16)v;
}

static s32 lim_ir0(s64 v)
{
    if (v < 0) {
        FLAG |= 1u << 12;
        return 0;
    }
    if (v > 0x1000) {
        FLAG |= 1u << 12;
        return 0x1000;
    }
    return (s32)v;
}

static s64 mac0_set(s64 v)
{
    if (v > 0x7FFFFFFFLL)
        FLAG |= 1u << 16;
    else if (v < -0x80000000LL)
        FLAG |= 1u << 15;
    MAC0 = (u32)(s32)v;
    return v;
}

static void push_color(s32 r, s32 g, s32 b)
{
    u8 cd = (u8)(RGBC >> 24);
    RGB0 = RGB1;
    RGB1 = RGB2;
    RGB2 = (u32)lim_color(1, r) | ((u32)lim_color(2, g) << 8) | ((u32)lim_color(3, b) << 16) | ((u32)cd << 24);
}

static void push_sz(s64 v)
{
    SZ0 = SZ1;
    SZ1 = SZ2;
    SZ2 = SZ3;
    SZ3 = lim_sz3(v);
}

/* --- UNR division (RTPS/RTPT) ------------------------------------------------ */

static const u8 unr_table[257] = {
    0xFF,0xFD,0xFB,0xF9,0xF7,0xF5,0xF3,0xF1,0xEF,0xEE,0xEC,0xEA,0xE8,0xE6,0xE4,0xE3,
    0xE1,0xDF,0xDD,0xDC,0xDA,0xD8,0xD6,0xD5,0xD3,0xD1,0xD0,0xCE,0xCD,0xCB,0xC9,0xC8,
    0xC6,0xC5,0xC3,0xC1,0xC0,0xBE,0xBD,0xBB,0xBA,0xB8,0xB7,0xB5,0xB4,0xB2,0xB1,0xB0,
    0xAE,0xAD,0xAB,0xAA,0xA9,0xA7,0xA6,0xA4,0xA3,0xA2,0xA0,0x9F,0x9E,0x9C,0x9B,0x9A,
    0x99,0x97,0x96,0x95,0x94,0x92,0x91,0x90,0x8F,0x8D,0x8C,0x8B,0x8A,0x89,0x87,0x86,
    0x85,0x84,0x83,0x82,0x81,0x7F,0x7E,0x7D,0x7C,0x7B,0x7A,0x79,0x78,0x77,0x75,0x74,
    0x73,0x72,0x71,0x70,0x6F,0x6E,0x6D,0x6C,0x6B,0x6A,0x69,0x68,0x67,0x66,0x65,0x64,
    0x63,0x62,0x61,0x60,0x5F,0x5E,0x5D,0x5D,0x5C,0x5B,0x5A,0x59,0x58,0x57,0x56,0x55,
    0x54,0x53,0x53,0x52,0x51,0x50,0x4F,0x4E,0x4D,0x4D,0x4C,0x4B,0x4A,0x49,0x48,0x48,
    0x47,0x46,0x45,0x44,0x43,0x43,0x42,0x41,0x40,0x3F,0x3F,0x3E,0x3D,0x3C,0x3C,0x3B,
    0x3A,0x39,0x39,0x38,0x37,0x36,0x36,0x35,0x34,0x33,0x33,0x32,0x31,0x31,0x30,0x2F,
    0x2E,0x2E,0x2D,0x2C,0x2C,0x2B,0x2A,0x2A,0x29,0x28,0x28,0x27,0x26,0x26,0x25,0x24,
    0x24,0x23,0x22,0x22,0x21,0x20,0x20,0x1F,0x1E,0x1E,0x1D,0x1D,0x1C,0x1B,0x1B,0x1A,
    0x19,0x19,0x18,0x18,0x17,0x16,0x16,0x15,0x15,0x14,0x14,0x13,0x12,0x12,0x11,0x11,
    0x10,0x0F,0x0F,0x0E,0x0E,0x0D,0x0D,0x0C,0x0C,0x0B,0x0A,0x0A,0x09,0x09,0x08,0x08,
    0x07,0x07,0x06,0x06,0x05,0x05,0x04,0x04,0x03,0x03,0x02,0x02,0x01,0x01,0x00,0x00,
    0x00
};

static int clz16(u16 v)
{
    int n = 0;
    if (v == 0)
        return 16;
    while (!(v & 0x8000)) {
        v <<= 1;
        n++;
    }
    return n;
}

static u32 unr_divide(u16 h, u16 sz3)
{
    int z;
    u32 n, d, u;
    if (h >= (u32)sz3 * 2) {
        FLAG |= 1u << 17;
        return 0x1FFFF;
    }
    z = clz16(sz3);
    n = (u32)h << z;
    d = (u32)sz3 << z;
    u = unr_table[((d & 0x7FFF) + 0x40) >> 7] + 0x101;
    d = ((0x2000080 - (d * u)) >> 8);
    d = ((0x0000080 + (d * u)) >> 8);
    n = (u32)(((uint64_t)n * d + 0x8000) >> 16);
    if (n > 0x1FFFF)
        n = 0x1FFFF;
    return n;
}

/* --- register access ---------------------------------------------------------- */

unsigned int psx_gte_read_data(int reg)
{
    switch (reg) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        return (u32)(s32)(s16)D[reg];
    case 7: case 16: case 17: case 18: case 19:
        return D[reg] & 0xFFFF;
    case 15:
        return SXY2;
    case 28: case 29: {
        s32 r = (s32)IR1 >> 7, g = (s32)IR2 >> 7, b = (s32)IR3 >> 7;
        if (r < 0) r = 0;
        if (r > 0x1F) r = 0x1F;
        if (g < 0) g = 0;
        if (g > 0x1F) g = 0x1F;
        if (b < 0) b = 0;
        if (b > 0x1F) b = 0x1F;
        return (u32)(r | (g << 5) | (b << 10));
    }
    case 31: {
        s32 v = (s32)D[30];
        u32 x = (v < 0) ? ~(u32)v : (u32)v;
        int n = 0;
        if (x == 0)
            return 32;
        while (!(x & 0x80000000u)) {
            x <<= 1;
            n++;
        }
        return (u32)n;
    }
    default:
        return D[reg];
    }
}

void psx_gte_write_data(int reg, unsigned int v)
{
    switch (reg) {
    case 15:
        SXY0 = SXY1;
        SXY1 = SXY2;
        SXY2 = v;
        break;
    case 28:
        D[28] = v & 0x7FFF;
        IR1 = (v & 0x1F) << 7;
        IR2 = ((v >> 5) & 0x1F) << 7;
        IR3 = ((v >> 10) & 0x1F) << 7;
        break;
    case 29: case 31:
        break; /* read-only */
    default:
        D[reg] = v;
        break;
    }
}

unsigned int psx_gte_read_ctrl(int reg)
{
    switch (reg) {
    case 4: case 12: case 20: case 26: case 27: case 29: case 30:
        return (u32)(s32)(s16)C[reg];
    case 31: {
        u32 f = FLAG & 0x7FFFF000u;
        if (f & 0x7F87E000u)
            f |= 0x80000000u;
        return f;
    }
    default:
        return C[reg];
    }
}

void psx_gte_write_ctrl(int reg, unsigned int v)
{
    if (reg == 31)
        v &= 0x7FFFF000u;
    C[reg] = v;
}

/* --- building blocks ------------------------------------------------------------ */

/* MAC = (T + M * V) >> sf*12, IR = lim(MAC) */
static void mat_vec(int m, const s32 *t, s32 v0, s32 v1, s32 v2, int sf, int lm)
{
    int i;
    for (i = 0; i < 3; i++) {
        s64 acc = (s64)(t ? t[i] : 0) << 12;
        acc = mac_ext(i + 1, acc + (s64)mat(m, i, 0) * v0);
        acc = mac_ext(i + 1, acc + (s64)mat(m, i, 1) * v1);
        acc = acc + (s64)mat(m, i, 2) * v2;
        lim_ir(i + 1, set_mac(i + 1, acc, sf ? 12 : 0), lm);
    }
}

static void rtp(int n, int sf, int lm, int last)
{
    s32 vx = VX(n), vy = VY(n), vz = VZ(n);
    s64 m1, m2, m3;
    s64 div, x, y;

    m1 = mac_ext(1, mac_ext(1, mac_ext(1, ((s64)TRX << 12) + (s64)mat(0, 0, 0) * vx) + (s64)mat(0, 0, 1) * vy) + (s64)mat(0, 0, 2) * vz);
    m2 = mac_ext(2, mac_ext(2, mac_ext(2, ((s64)TRY << 12) + (s64)mat(0, 1, 0) * vx) + (s64)mat(0, 1, 1) * vy) + (s64)mat(0, 1, 2) * vz);
    m3 = mac_ext(3, mac_ext(3, mac_ext(3, ((s64)TRZ << 12) + (s64)mat(0, 2, 0) * vx) + (s64)mat(0, 2, 1) * vy) + (s64)mat(0, 2, 2) * vz);

    MAC1 = (u32)(s32)(m1 >> (sf ? 12 : 0));
    MAC2 = (u32)(s32)(m2 >> (sf ? 12 : 0));
    MAC3 = (u32)(s32)(m3 >> (sf ? 12 : 0));
    lim_ir(1, (s32)MAC1, lm);
    lim_ir(2, (s32)MAC2, lm);
    lim_ir3_rtp(m3, sf, lm);

    push_sz(m3 >> 12);
    div = unr_divide(H, (u16)SZ3);

    /* widescreen: squeeze x by 3/4, the picture is shown stretched to 16:9 */
    x = mac0_set((psx_gte_widescreen ? div * (s16)IR1 * 3 / 4 : div * (s16)IR1) + OFX);
    y = mac0_set(div * (s16)IR2 + OFY);
    SXY0 = SXY1;
    SXY1 = SXY2;
    SXY2 = (u16)lim_sxy(1, (s32)(x >> 16)) | ((u32)(u16)lim_sxy(2, (s32)(y >> 16)) << 16);

    if (last) {
        s64 d = mac0_set(div * DQA + DQB);
        IR0 = (u32)lim_ir0(d >> 12);
    }
}

/* colour stage: MAC = (BK + LCM * IR) , then optional depth cue */
static void light_color_stage(int sf, int lm)
{
    s32 bk[3] = { RBK, GBK, BBK };
    s32 v0 = (s16)IR1, v1 = (s16)IR2, v2 = (s16)IR3;
    mat_vec(2, bk, v0, v1, v2, sf, lm);
}

static void normal_stage(int n, int sf, int lm)
{
    mat_vec(1, NULL, VX(n), VY(n), VZ(n), sf, lm);
}

/* MAC = (RGB << 4) * IR, used by NCC/CC */
static void color_mul_ir(int sf, int lm)
{
    s32 r = (s32)(RGBC & 0xFF), g = (s32)((RGBC >> 8) & 0xFF), b = (s32)((RGBC >> 16) & 0xFF);
    lim_ir(1, set_mac(1, ((s64)r << 4) * (s16)IR1, sf ? 12 : 0), lm);
    lim_ir(2, set_mac(2, ((s64)g << 4) * (s16)IR2, sf ? 12 : 0), lm);
    lim_ir(3, set_mac(3, ((s64)b << 4) * (s16)IR3, sf ? 12 : 0), lm);
}

/* depth cue: MAC = MAC + (FC - MAC) * IR0 (inputs given as MAC<<12 values) */
static void depth_cue(s64 m1, s64 m2, s64 m3, int sf, int lm)
{
    s32 i1, i2, i3;
    i1 = lim_ir(1, (s32)(mac_ext(1, ((s64)RFC << 12) - m1) >> (sf ? 12 : 0)), 0);
    i2 = lim_ir(2, (s32)(mac_ext(2, ((s64)GFC << 12) - m2) >> (sf ? 12 : 0)), 0);
    i3 = lim_ir(3, (s32)(mac_ext(3, ((s64)BFC << 12) - m3) >> (sf ? 12 : 0)), 0);
    lim_ir(1, set_mac(1, (s64)i1 * (s16)IR0 + m1, sf ? 12 : 0), lm);
    lim_ir(2, set_mac(2, (s64)i2 * (s16)IR0 + m2, sf ? 12 : 0), lm);
    lim_ir(3, set_mac(3, (s64)i3 * (s16)IR0 + m3, sf ? 12 : 0), lm);
}

static void finish_color(void)
{
    push_color((s32)MAC1 >> 4, (s32)MAC2 >> 4, (s32)MAC3 >> 4);
}

static void ncd(int n, int sf, int lm)
{
    s32 r, g, b;
    normal_stage(n, sf, lm);
    light_color_stage(sf, lm);
    r = (s32)(RGBC & 0xFF); g = (s32)((RGBC >> 8) & 0xFF); b = (s32)((RGBC >> 16) & 0xFF);
    depth_cue(((s64)r << 4) * (s16)IR1, ((s64)g << 4) * (s16)IR2, ((s64)b << 4) * (s16)IR3, sf, lm);
    finish_color();
}

static void ncc(int n, int sf, int lm)
{
    normal_stage(n, sf, lm);
    light_color_stage(sf, lm);
    color_mul_ir(sf, lm);
    finish_color();
}

static void nc(int n, int sf, int lm)
{
    normal_stage(n, sf, lm);
    light_color_stage(sf, lm);
    finish_color();
}

/* --- command dispatch ------------------------------------------------------------- */

void psx_gte_command(unsigned int cmd)
{
    int sf = (cmd >> 19) & 1;
    int lm = (cmd >> 10) & 1;
    int shift = sf ? 12 : 0;

    FLAG = 0;

    switch (cmd & 0x3F) {
    case 0x01: /* RTPS */
        rtp(0, sf, lm, 1);
        break;

    case 0x30: /* RTPT */
        rtp(0, sf, lm, 0);
        rtp(1, sf, lm, 0);
        rtp(2, sf, lm, 1);
        break;

    case 0x06: { /* NCLIP */
        s32 x0 = (s16)SXY0, y0 = (s16)(SXY0 >> 16);
        s32 x1 = (s16)SXY1, y1 = (s16)(SXY1 >> 16);
        s32 x2 = (s16)SXY2, y2 = (s16)(SXY2 >> 16);
        mac0_set((s64)x0 * y1 + (s64)x1 * y2 + (s64)x2 * y0 - (s64)x0 * y2 - (s64)x1 * y0 - (s64)x2 * y1);
        break;
    }

    case 0x0C: { /* OP (outer product) */
        s32 d1 = mat(0, 0, 0), d2 = mat(0, 1, 1), d3 = mat(0, 2, 2);
        s32 i1 = (s16)IR1, i2 = (s16)IR2, i3 = (s16)IR3;
        lim_ir(1, set_mac(1, (s64)i3 * d2 - (s64)i2 * d3, shift), lm);
        lim_ir(2, set_mac(2, (s64)i1 * d3 - (s64)i3 * d1, shift), lm);
        lim_ir(3, set_mac(3, (s64)i2 * d1 - (s64)i1 * d2, shift), lm);
        break;
    }

    case 0x10: { /* DPCS */
        s32 r = (s32)(RGBC & 0xFF), g = (s32)((RGBC >> 8) & 0xFF), b = (s32)((RGBC >> 16) & 0xFF);
        depth_cue((s64)r << 16, (s64)g << 16, (s64)b << 16, sf, lm);
        finish_color();
        break;
    }

    case 0x2A: { /* DPCT */
        int i;
        for (i = 0; i < 3; i++) {
            s32 r = (s32)(RGB0 & 0xFF), g = (s32)((RGB0 >> 8) & 0xFF), b = (s32)((RGB0 >> 16) & 0xFF);
            depth_cue((s64)r << 16, (s64)g << 16, (s64)b << 16, sf, lm);
            finish_color();
        }
        break;
    }

    case 0x11: /* INTPL */
        depth_cue((s64)(s16)IR1 << 12, (s64)(s16)IR2 << 12, (s64)(s16)IR3 << 12, sf, lm);
        finish_color();
        break;

    case 0x12: { /* MVMVA */
        int mx = (cmd >> 17) & 3, vsel = (cmd >> 15) & 3, cv = (cmd >> 13) & 3;
        s32 v0, v1, v2, t[3];
        if (vsel < 3) {
            v0 = VX(vsel); v1 = VY(vsel); v2 = VZ(vsel);
        } else {
            v0 = (s16)IR1; v1 = (s16)IR2; v2 = (s16)IR3;
        }
        switch (cv) {
        case 0: t[0] = TRX; t[1] = TRY; t[2] = TRZ; break;
        case 1: t[0] = RBK; t[1] = GBK; t[2] = BBK; break;
        case 2: t[0] = RFC; t[1] = GFC; t[2] = BFC; break; /* hardware bug ignored */
        default: t[0] = t[1] = t[2] = 0; break;
        }
        if (mx == 3) {
            /* "garbage" matrix; not used by well-behaved code */
            mx = 0;
        }
        mat_vec(mx, t, v0, v1, v2, sf, lm);
        break;
    }

    case 0x13: ncd(0, sf, lm); break;                                       /* NCDS */
    case 0x16: ncd(0, sf, lm); ncd(1, sf, lm); ncd(2, sf, lm); break;        /* NCDT */
    case 0x1B: ncc(0, sf, lm); break;                                       /* NCCS */
    case 0x3F: ncc(0, sf, lm); ncc(1, sf, lm); ncc(2, sf, lm); break;        /* NCCT */
    case 0x1E: nc(0, sf, lm); break;                                        /* NCS */
    case 0x20: nc(0, sf, lm); nc(1, sf, lm); nc(2, sf, lm); break;           /* NCT */

    case 0x14: { /* CDP */
        s32 r = (s32)(RGBC & 0xFF), g = (s32)((RGBC >> 8) & 0xFF), b = (s32)((RGBC >> 16) & 0xFF);
        light_color_stage(sf, lm);
        depth_cue(((s64)r << 4) * (s16)IR1, ((s64)g << 4) * (s16)IR2, ((s64)b << 4) * (s16)IR3, sf, lm);
        finish_color();
        break;
    }

    case 0x1C: /* CC */
        light_color_stage(sf, lm);
        color_mul_ir(sf, lm);
        finish_color();
        break;

    case 0x29: { /* DCPL */
        s32 r = (s32)(RGBC & 0xFF), g = (s32)((RGBC >> 8) & 0xFF), b = (s32)((RGBC >> 16) & 0xFF);
        depth_cue(((s64)r << 4) * (s16)IR1, ((s64)g << 4) * (s16)IR2, ((s64)b << 4) * (s16)IR3, sf, lm);
        finish_color();
        break;
    }

    case 0x28: /* SQR */
        lim_ir(1, set_mac(1, (s64)(s16)IR1 * (s16)IR1, shift), lm);
        lim_ir(2, set_mac(2, (s64)(s16)IR2 * (s16)IR2, shift), lm);
        lim_ir(3, set_mac(3, (s64)(s16)IR3 * (s16)IR3, shift), lm);
        break;

    case 0x2D: { /* AVSZ3 */
        s64 v = mac0_set((s64)ZSF3 * ((u16)SZ1 + (u16)SZ2 + (u16)SZ3));
        OTZ = lim_sz3(v >> 12);
        break;
    }

    case 0x2E: { /* AVSZ4 */
        s64 v = mac0_set((s64)ZSF4 * ((u16)SZ0 + (u16)SZ1 + (u16)SZ2 + (u16)SZ3));
        OTZ = lim_sz3(v >> 12);
        break;
    }

    case 0x3D: /* GPF */
        lim_ir(1, set_mac(1, (s64)(s16)IR0 * (s16)IR1, shift), lm);
        lim_ir(2, set_mac(2, (s64)(s16)IR0 * (s16)IR2, shift), lm);
        lim_ir(3, set_mac(3, (s64)(s16)IR0 * (s16)IR3, shift), lm);
        finish_color();
        break;

    case 0x3E: /* GPL */
        lim_ir(1, set_mac(1, ((s64)(s32)MAC1 << shift) + (s64)(s16)IR0 * (s16)IR1, shift), lm);
        lim_ir(2, set_mac(2, ((s64)(s32)MAC2 << shift) + (s64)(s16)IR0 * (s16)IR2, shift), lm);
        lim_ir(3, set_mac(3, ((s64)(s32)MAC3 << shift) + (s64)(s16)IR0 * (s16)IR3, shift), lm);
        finish_color();
        break;

    default:
        psx_log("GTE: unknown command 0x%07X\n", cmd);
        break;
    }

    if (FLAG & 0x7F87E000u)
        FLAG |= 0x80000000u;
}
