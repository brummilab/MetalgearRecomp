/*
 * libgte replacement: matrix/vector helpers and trigonometry.
 *
 * Functions that the original library implements on the GTE are implemented
 * on the GTE emulator as well, so they produce identical saturation and keep
 * the same side effect (the matrix argument ends up in the GTE's rotation
 * registers, as the PsyQ documentation warns).
 */
#include "psx.h"

#include <math.h>

unsigned int psx_gte_read_data(int reg);
void         psx_gte_write_data(int reg, unsigned int value);
unsigned int psx_gte_read_ctrl(int reg);
void         psx_gte_write_ctrl(int reg, unsigned int value);
void         psx_gte_command(unsigned int cmd);

#define ONE 4096

/* GTE command words (lower 25 bits of the COP2 instruction) */
#define CMD_RTPS    0x0180001
#define CMD_RTPT    0x0280030
#define CMD_MVMVA_RT_V0   0x0486012  /* sf=1, RT * V0, no translation */
#define CMD_MVMVA_RT_IR   0x049E012  /* sf=1, RT * IR, no translation */

/* ---- trigonometry ----------------------------------------------------------- */

static short sin_table[4096];
static int   trig_ready;

static void trig_init(void)
{
    int i;
    for (i = 0; i < 4096; i++)
        sin_table[i] = (short)lround(4096.0 * sin(i * (2.0 * M_PI / 4096.0)));
    trig_ready = 1;
}

int rsin(int a)
{
    if (!trig_ready)
        trig_init();
    return sin_table[a & 4095];
}

int rcos(int a)
{
    if (!trig_ready)
        trig_init();
    return sin_table[(a + 1024) & 4095];
}

long ratan2(long y, long x)
{
    if (x == 0 && y == 0)
        return 0;
    return (long)lround(atan2((double)y, (double)x) * (2048.0 / M_PI));
}

long SquareRoot0(long a)
{
    unsigned long v = (unsigned long)a, r = 0, bit = 1ul << 30;
    if (a <= 0)
        return 0;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (long)r;
}

/* ---- GTE register helpers ------------------------------------------------------ */

static void load_matrix(int base, const MATRIX *m)
{
    psx_gte_write_ctrl(base + 0, (u_short)m->m[0][0] | ((unsigned)(u_short)m->m[0][1] << 16));
    psx_gte_write_ctrl(base + 1, (u_short)m->m[0][2] | ((unsigned)(u_short)m->m[1][0] << 16));
    psx_gte_write_ctrl(base + 2, (u_short)m->m[1][1] | ((unsigned)(u_short)m->m[1][2] << 16));
    psx_gte_write_ctrl(base + 3, (u_short)m->m[2][0] | ((unsigned)(u_short)m->m[2][1] << 16));
    psx_gte_write_ctrl(base + 4, (u_short)m->m[2][2]);
}

static void read_matrix(int base, MATRIX *m)
{
    unsigned w0 = psx_gte_read_ctrl(base + 0), w1 = psx_gte_read_ctrl(base + 1);
    unsigned w2 = psx_gte_read_ctrl(base + 2), w3 = psx_gte_read_ctrl(base + 3);
    unsigned w4 = psx_gte_read_ctrl(base + 4);
    m->m[0][0] = (short)w0; m->m[0][1] = (short)(w0 >> 16);
    m->m[0][2] = (short)w1; m->m[1][0] = (short)(w1 >> 16);
    m->m[1][1] = (short)w2; m->m[1][2] = (short)(w2 >> 16);
    m->m[2][0] = (short)w3; m->m[2][1] = (short)(w3 >> 16);
    m->m[2][2] = (short)w4;
}

static void load_sv0(short x, short y, short z)
{
    psx_gte_write_data(0, (u_short)x | ((unsigned)(u_short)y << 16));
    psx_gte_write_data(1, (unsigned)(int)z);
}

/* RT * (x,y,z) >> 12 into out (IR, saturated) and outl (MAC) */
static void rt_mul(short x, short y, short z, short *out, long *outl)
{
    load_sv0(x, y, z);
    psx_gte_command(CMD_MVMVA_RT_V0);
    if (out) {
        out[0] = (short)psx_gte_read_data(9);
        out[1] = (short)psx_gte_read_data(10);
        out[2] = (short)psx_gte_read_data(11);
    }
    if (outl) {
        outl[0] = (long)(int)psx_gte_read_data(25);
        outl[1] = (long)(int)psx_gte_read_data(26);
        outl[2] = (long)(int)psx_gte_read_data(27);
    }
}

/* ---- GTE setup ----------------------------------------------------------------- */

void InitGeom(void)
{
    psx_gte_write_ctrl(24, 0);       /* OFX */
    psx_gte_write_ctrl(25, 0);       /* OFY */
    psx_gte_write_ctrl(26, 1000);    /* H */
    psx_gte_write_ctrl(27, 0xEF9E);  /* DQA */
    psx_gte_write_ctrl(28, 0x1400000); /* DQB */
    psx_gte_write_ctrl(29, 0x155);   /* ZSF3 */
    psx_gte_write_ctrl(30, 0x100);   /* ZSF4 */
}

void psx_gte_note_rt(unsigned int addr);

void SetRotMatrix(MATRIX *m)
{
    load_matrix(0, m);
    psx_gte_note_rt((unsigned int)(uintptr_t)m);
}

void SetTransMatrix(MATRIX *m)
{
    psx_gte_write_ctrl(5, (unsigned)m->t[0]);
    psx_gte_write_ctrl(6, (unsigned)m->t[1]);
    psx_gte_write_ctrl(7, (unsigned)m->t[2]);
}

void ReadRotMatrix(MATRIX *m)
{
    read_matrix(0, m);
    m->t[0] = (long)(int)psx_gte_read_ctrl(5);
    m->t[1] = (long)(int)psx_gte_read_ctrl(6);
    m->t[2] = (long)(int)psx_gte_read_ctrl(7);
}

/* ---- matrix products ------------------------------------------------------------- */

/* dst = RT * src (column by column); dst may alias src */
static void rt_mul_matrix(const MATRIX *src, MATRIX *dst)
{
    short col[3][3];
    int c;
    for (c = 0; c < 3; c++)
        rt_mul(src->m[0][c], src->m[1][c], src->m[2][c], col[c], NULL);
    for (c = 0; c < 3; c++) {
        dst->m[0][c] = col[c][0];
        dst->m[1][c] = col[c][1];
        dst->m[2][c] = col[c][2];
    }
}

MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
    load_matrix(0, m0);
    rt_mul_matrix(m1, m2);
    return m2;
}

MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1)
{
    load_matrix(0, m0);
    rt_mul_matrix(m1, m0);
    return m0;
}

MATRIX *MulMatrix2(MATRIX *m0, MATRIX *m1)
{
    load_matrix(0, m0);
    rt_mul_matrix(m1, m1);
    return m1;
}

MATRIX *MulRotMatrix(MATRIX *m0)
{
    rt_mul_matrix(m0, m0);
    return m0;
}

MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v)
{
    int r;
    for (r = 0; r < 3; r++) {
        m->m[r][0] = (short)((m->m[r][0] * v->vx) >> 12);
        m->m[r][1] = (short)((m->m[r][1] * v->vy) >> 12);
        m->m[r][2] = (short)((m->m[r][2] * v->vz) >> 12);
    }
    return m;
}

/* ---- vector products --------------------------------------------------------------- */

VECTOR *ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1)
{
    long out[3];
    load_matrix(0, m);
    rt_mul(v0->vx, v0->vy, v0->vz, NULL, out);
    v1->vx = out[0]; v1->vy = out[1]; v1->vz = out[2];
    return v1;
}

SVECTOR *ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1)
{
    short out[3];
    load_matrix(0, m);
    rt_mul(v0->vx, v0->vy, v0->vz, out, NULL);
    v1->vx = out[0]; v1->vy = out[1]; v1->vz = out[2];
    return v1;
}

static void mat_mul_long(const MATRIX *m, const VECTOR *v0, VECTOR *v1)
{
    int64_t r[3];
    int i;
    for (i = 0; i < 3; i++)
        r[i] = ((int64_t)m->m[i][0] * v0->vx + (int64_t)m->m[i][1] * v0->vy + (int64_t)m->m[i][2] * v0->vz) >> 12;
    v1->vx = (long)r[0]; v1->vy = (long)r[1]; v1->vz = (long)r[2];
}

VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1)
{
    load_matrix(0, m);
    mat_mul_long(m, v0, v1);
    return v1;
}

VECTOR *ApplyRotMatrix(SVECTOR *v0, VECTOR *v1)
{
    long out[3];
    rt_mul(v0->vx, v0->vy, v0->vz, NULL, out);
    v1->vx = out[0]; v1->vy = out[1]; v1->vz = out[2];
    return v1;
}

VECTOR *ApplyRotMatrixLV(VECTOR *v0, VECTOR *v1)
{
    MATRIX m;
    read_matrix(0, &m);
    mat_mul_long(&m, v0, v1);
    return v1;
}

VECTOR *Square0(VECTOR *v0, VECTOR *v1)
{
    v1->vx = v0->vx * v0->vx;
    v1->vy = v0->vy * v0->vy;
    v1->vz = v0->vz * v0->vz;
    return v1;
}

void OuterProduct12(VECTOR *v0, VECTOR *v1, VECTOR *v2)
{
    long x = (v0->vy * v1->vz - v0->vz * v1->vy) >> 12;
    long y = (v0->vz * v1->vx - v0->vx * v1->vz) >> 12;
    long z = (v0->vx * v1->vy - v0->vy * v1->vx) >> 12;
    v2->vx = x; v2->vy = y; v2->vz = z;
}

long VectorNormal(VECTOR *v0, VECTOR *v1)
{
    int64_t sq = (int64_t)v0->vx * v0->vx + (int64_t)v0->vy * v0->vy + (int64_t)v0->vz * v0->vz;
    double len = sqrt((double)sq);
    if (len == 0) {
        v1->vx = v1->vy = v1->vz = 0;
        return 0;
    }
    v1->vx = (long)(v0->vx * 4096.0 / len);
    v1->vy = (long)(v0->vy * 4096.0 / len);
    v1->vz = (long)(v0->vz * 4096.0 / len);
    return (long)sq;
}

long VectorNormalSS(SVECTOR *v0, SVECTOR *v1)
{
    int64_t sq = (int64_t)v0->vx * v0->vx + (int64_t)v0->vy * v0->vy + (int64_t)v0->vz * v0->vz;
    double len = sqrt((double)sq);
    if (len == 0) {
        v1->vx = v1->vy = v1->vz = 0;
        return 0;
    }
    v1->vx = (short)(v0->vx * 4096.0 / len);
    v1->vy = (short)(v0->vy * 4096.0 / len);
    v1->vz = (short)(v0->vz * 4096.0 / len);
    return (long)sq;
}

/* ---- rotation matrices ----------------------------------------------------------- */

static void mat_mul3(const short a[3][3], const short b[3][3], short out[3][3])
{
    short t[3][3];
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            t[i][j] = (short)((a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j]) >> 12);
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            out[i][j] = t[i][j];
}

static void rot_x(int a, short m[3][3])
{
    short s = (short)rsin(a), c = (short)rcos(a);
    m[0][0] = ONE; m[0][1] = 0; m[0][2] = 0;
    m[1][0] = 0;   m[1][1] = c; m[1][2] = (short)-s;
    m[2][0] = 0;   m[2][1] = s; m[2][2] = c;
}

static void rot_y(int a, short m[3][3])
{
    short s = (short)rsin(a), c = (short)rcos(a);
    m[0][0] = c;          m[0][1] = 0;   m[0][2] = s;
    m[1][0] = 0;          m[1][1] = ONE; m[1][2] = 0;
    m[2][0] = (short)-s;  m[2][1] = 0;   m[2][2] = c;
}

static void rot_z(int a, short m[3][3])
{
    short s = (short)rsin(a), c = (short)rcos(a);
    m[0][0] = c; m[0][1] = (short)-s; m[0][2] = 0;
    m[1][0] = s; m[1][1] = c;         m[1][2] = 0;
    m[2][0] = 0; m[2][1] = 0;         m[2][2] = ONE;
}

/* m = R1 * R2 * R3 */
static MATRIX *rot3(MATRIX *m, void (*r1)(int, short[3][3]), int a1,
                    void (*r2)(int, short[3][3]), int a2,
                    void (*r3)(int, short[3][3]), int a3)
{
    short x[3][3], y[3][3], z[3][3];
    r1(a1, x);
    r2(a2, y);
    r3(a3, z);
    mat_mul3(x, y, x);
    mat_mul3(x, z, m->m);
    return m;
}

MATRIX *RotMatrix(SVECTOR *r, MATRIX *m)
{
    return rot3(m, rot_x, r->vx, rot_y, r->vy, rot_z, r->vz);
}

MATRIX *RotMatrixYXZ(SVECTOR *r, MATRIX *m)
{
    return rot3(m, rot_y, r->vy, rot_x, r->vx, rot_z, r->vz);
}

MATRIX *RotMatrixZYX(SVECTOR *r, MATRIX *m)
{
    return rot3(m, rot_z, r->vz, rot_y, r->vy, rot_x, r->vx);
}

MATRIX *RotMatrixYXZ_gte(SVECTOR *r, MATRIX *m) { return RotMatrixYXZ(r, m); }
MATRIX *RotMatrixZYX_gte(SVECTOR *r, MATRIX *m) { return RotMatrixZYX(r, m); }

static MATRIX *rot_apply(MATRIX *m, void (*rot)(int, short[3][3]), long a)
{
    short r[3][3];
    rot((int)a, r);
    mat_mul3(r, m->m, m->m);
    return m;
}

MATRIX *RotMatrixX(long r, MATRIX *m) { return rot_apply(m, rot_x, r); }
MATRIX *RotMatrixY(long r, MATRIX *m) { return rot_apply(m, rot_y, r); }
MATRIX *RotMatrixZ(long r, MATRIX *m) { return rot_apply(m, rot_z, r); }

/* ---- perspective transformation ---------------------------------------------------- */

long RotTransPers(SVECTOR *v0, long *sxy, long *p, long *flag)
{
    load_sv0(v0->vx, v0->vy, v0->vz);
    psx_gte_command(CMD_RTPS);
    *sxy = (long)psx_gte_read_data(14);
    *p = (long)(int)psx_gte_read_data(8);
    *flag = (long)psx_gte_read_ctrl(31);
    return (long)(psx_gte_read_data(19) >> 2);
}

long RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
                   long *sxy0, long *sxy1, long *sxy2, long *p, long *flag)
{
    psx_gte_write_data(0, (u_short)v0->vx | ((unsigned)(u_short)v0->vy << 16));
    psx_gte_write_data(1, (unsigned)(int)v0->vz);
    psx_gte_write_data(2, (u_short)v1->vx | ((unsigned)(u_short)v1->vy << 16));
    psx_gte_write_data(3, (unsigned)(int)v1->vz);
    psx_gte_write_data(4, (u_short)v2->vx | ((unsigned)(u_short)v2->vy << 16));
    psx_gte_write_data(5, (unsigned)(int)v2->vz);
    psx_gte_command(CMD_RTPT);
    *sxy0 = (long)psx_gte_read_data(12);
    *sxy1 = (long)psx_gte_read_data(13);
    *sxy2 = (long)psx_gte_read_data(14);
    *p = (long)(int)psx_gte_read_data(8);
    *flag = (long)psx_gte_read_ctrl(31);
    return (long)(psx_gte_read_data(19) >> 2);
}

/* ---- more helpers (used by stage overlays) ----------------------------------------- */

void SetLightMatrix(MATRIX *m) { load_matrix(8, m); }
void SetColorMatrix(MATRIX *m) { load_matrix(16, m); }

MATRIX *RotMatrix_gte(SVECTOR *r, MATRIX *m) { return RotMatrix(r, m); }

/* m2 = m0 * m1 including translation: t2 = m0 * t1 + t0 */
MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2)
{
    VECTOR t;
    long t0[3];
    t0[0] = m0->t[0]; t0[1] = m0->t[1]; t0[2] = m0->t[2];
    t.vx = m1->t[0]; t.vy = m1->t[1]; t.vz = m1->t[2];
    mat_mul_long(m0, &t, &t);
    MulMatrix0(m0, m1, m2);
    m2->t[0] = t.vx + t0[0];
    m2->t[1] = t.vy + t0[1];
    m2->t[2] = t.vz + t0[2];
    return m2;
}

MATRIX *MulRotMatrix0(MATRIX *m0, MATRIX *m1)
{
    rt_mul_matrix(m0, m1);
    return m1;
}

void OuterProduct0(VECTOR *v0, VECTOR *v1, VECTOR *v2)
{
    long x = v0->vy * v1->vz - v0->vz * v1->vy;
    long y = v0->vz * v1->vx - v0->vx * v1->vz;
    long z = v0->vx * v1->vy - v0->vy * v1->vx;
    v2->vx = x; v2->vy = y; v2->vz = z;
}

void VectorNormalS(VECTOR *v0, SVECTOR *v1)
{
    int64_t sq = (int64_t)v0->vx * v0->vx + (int64_t)v0->vy * v0->vy + (int64_t)v0->vz * v0->vz;
    double len = sqrt((double)sq);
    if (len == 0) {
        v1->vx = v1->vy = v1->vz = 0;
        return;
    }
    v1->vx = (short)(v0->vx * 4096.0 / len);
    v1->vy = (short)(v0->vy * 4096.0 / len);
    v1->vz = (short)(v0->vz * 4096.0 / len);
}

/* light a normal with colour (GTE NCCS) */
void NormalColorCol(SVECTOR *v0, CVECTOR *v1, CVECTOR *v2)
{
    load_sv0(v0->vx, v0->vy, v0->vz);
    psx_gte_write_data(6, v1->r | (v1->g << 8) | (v1->b << 16) | ((unsigned)v1->cd << 24));
    psx_gte_command(0x108041B);
    {
        unsigned c = psx_gte_read_data(22);
        v2->r = (u_char)c;
        v2->g = (u_char)(c >> 8);
        v2->b = (u_char)(c >> 16);
        v2->cd = (u_char)(c >> 24);
    }
}
