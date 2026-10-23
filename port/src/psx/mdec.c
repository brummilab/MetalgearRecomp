/*
 * Movie decoding: libpress (DecDCTvlc2, DecDCTin/out) on top of an MDEC
 * emulation.
 *
 * DecDCTvlc2() turns an STR frame bitstream (versions 2 and 3, MPEG-1 style
 * Huffman codes) into MDEC run-level halfwords, like the original library.
 * DecDCTin() then decodes all macroblocks (dequantisation with the default
 * PSX quantisation table, 8x8 IDCT, YCbCr -> RGB) and DecDCTout() hands them
 * out in the order the MDEC produces them: column by column, top to bottom.
 *
 * The console finishes DecDCTout() with a DMA interrupt; here the output is
 * delivered right away and the callback runs synchronously. A request made
 * from inside the callback is served by the loop in serve(), not by
 * recursion.
 */
#include "psx.h"

#include <math.h>
#include <string.h>

/* ---- STR bitstream -> MDEC codes ------------------------------------------------- */

typedef struct {
    const uint8_t *data;
    size_t         size;   /* bytes */
    size_t         pos;    /* bit position */
} Bits;

/* STR bitstreams are 16-bit little-endian words read from the top bit */
static unsigned word_at(const Bits *b, size_t index)
{
    size_t o = index * 2;
    if (o + 1 >= b->size)
        return 0;
    return b->data[o] | (b->data[o + 1] << 8);
}

/* next n (<= 16) bits without consuming them */
static unsigned peek(const Bits *b, int n)
{
    size_t w = b->pos >> 4;
    uint32_t v = (word_at(b, w) << 16) | word_at(b, w + 1);
    return (v << (b->pos & 15)) >> (32 - n);
}

static unsigned get(Bits *b, int n)
{
    unsigned v = peek(b, n);
    b->pos += n;
    return v;
}

static int bit_at(const Bits *b, size_t pos)
{
    return (word_at(b, pos >> 4) >> (15 - (pos & 15))) & 1;
}

static int exhausted(const Bits *b) { return (b->pos >> 3) + 2 > b->size; }

/* MPEG-1 table B.14 (without the sign bit); "10" = end of block */
typedef struct { const char *code; uint8_t run; uint8_t level; } AcCode;

static const AcCode ac_codes[] = {
    { "11", 0, 1 }, { "011", 1, 1 }, { "0100", 0, 2 }, { "0101", 2, 1 },
    { "00101", 0, 3 }, { "00111", 3, 1 }, { "00110", 4, 1 },
    { "000110", 1, 2 }, { "000111", 5, 1 }, { "000101", 6, 1 }, { "000100", 7, 1 },
    { "0000110", 0, 4 }, { "0000100", 2, 2 }, { "0000111", 8, 1 }, { "0000101", 9, 1 },
    { "00100110", 0, 5 }, { "00100001", 0, 6 }, { "00100101", 1, 3 }, { "00100100", 3, 2 },
    { "00100111", 10, 1 }, { "00100011", 11, 1 }, { "00100010", 12, 1 }, { "00100000", 13, 1 },
    { "0000001010", 0, 7 }, { "0000001100", 1, 4 }, { "0000001011", 2, 3 }, { "0000001111", 4, 2 },
    { "0000001001", 5, 2 }, { "0000001110", 14, 1 }, { "0000001101", 15, 1 }, { "0000001000", 16, 1 },
    { "000000011101", 0, 8 }, { "000000011000", 0, 9 }, { "000000010011", 0, 10 }, { "000000010000", 0, 11 },
    { "000000011011", 1, 5 }, { "000000010100", 2, 4 }, { "000000011100", 3, 3 }, { "000000010010", 4, 3 },
    { "000000011110", 6, 2 }, { "000000010101", 7, 2 }, { "000000010001", 8, 2 }, { "000000011111", 17, 1 },
    { "000000011010", 18, 1 }, { "000000011001", 19, 1 }, { "000000010111", 20, 1 }, { "000000010110", 21, 1 },
    { "0000000011010", 0, 12 }, { "0000000011001", 0, 13 }, { "0000000011000", 0, 14 }, { "0000000010111", 0, 15 },
    { "0000000010110", 1, 6 }, { "0000000010101", 1, 7 }, { "0000000010100", 2, 5 }, { "0000000010011", 3, 4 },
    { "0000000010010", 5, 3 }, { "0000000010001", 9, 2 }, { "0000000010000", 10, 2 }, { "0000000011111", 22, 1 },
    { "0000000011110", 23, 1 }, { "0000000011101", 24, 1 }, { "0000000011100", 25, 1 }, { "0000000011011", 26, 1 },
    { "00000000011111", 0, 16 }, { "00000000011110", 0, 17 }, { "00000000011101", 0, 18 }, { "00000000011100", 0, 19 },
    { "00000000011011", 0, 20 }, { "00000000011010", 0, 21 }, { "00000000011001", 0, 22 }, { "00000000011000", 0, 23 },
    { "00000000010111", 0, 24 }, { "00000000010110", 0, 25 }, { "00000000010101", 0, 26 }, { "00000000010100", 0, 27 },
    { "00000000010011", 0, 28 }, { "00000000010010", 0, 29 }, { "00000000010001", 0, 30 }, { "00000000010000", 0, 31 },
    { "000000000011000", 0, 32 }, { "000000000010111", 0, 33 }, { "000000000010110", 0, 34 }, { "000000000010101", 0, 35 },
    { "000000000010100", 0, 36 }, { "000000000010011", 0, 37 }, { "000000000010010", 0, 38 }, { "000000000010001", 0, 39 },
    { "000000000010000", 0, 40 }, { "000000000011111", 1, 8 }, { "000000000011110", 1, 9 }, { "000000000011101", 1, 10 },
    { "000000000011100", 1, 11 }, { "000000000011011", 1, 12 }, { "000000000011010", 1, 13 }, { "000000000011001", 1, 14 },
    { "0000000000010011", 1, 15 }, { "0000000000010010", 1, 16 }, { "0000000000010001", 1, 17 }, { "0000000000010000", 1, 18 },
    { "0000000000010100", 6, 3 }, { "0000000000011010", 11, 2 }, { "0000000000011001", 12, 2 }, { "0000000000011000", 13, 2 },
    { "0000000000010111", 14, 2 }, { "0000000000010110", 15, 2 }, { "0000000000010101", 16, 2 }, { "0000000000011111", 27, 1 },
    { "0000000000011110", 28, 1 }, { "0000000000011101", 29, 1 }, { "0000000000011100", 30, 1 }, { "0000000000011011", 31, 1 },
};

#define NUM_AC (sizeof(ac_codes) / sizeof(ac_codes[0]))

/* lookup by the next 16 bits: code length (0 = invalid), run, level */
static struct AcCode { uint8_t len, run, level; } *ac_lut;
PSX_HEAP_BUFFER(ac_lut, (1 << 16) * sizeof(struct AcCode))
static int ac_ready;

static void ac_init(void)
{
    size_t i;
    for (i = 0; i < NUM_AC; i++) {
        const char *c = ac_codes[i].code;
        unsigned v = 0, n = 0, fill, f;
        for (; *c; c++, n++)
            v = (v << 1) | (*c == '1');
        fill = 1u << (16 - n);
        for (f = 0; f < fill; f++) {
            unsigned idx = (v << (16 - n)) | f;
            ac_lut[idx].len = (uint8_t)n;
            ac_lut[idx].run = ac_codes[i].run;
            ac_lut[idx].level = ac_codes[i].level;
        }
    }
    ac_ready = 1;
}

/* DC size codes of STR v3 (MPEG-1 tables B.12/B.13) */
static const struct { const char *code; int size; } dc_luma[] = {
    { "100", 0 }, { "00", 1 }, { "01", 2 }, { "101", 3 }, { "110", 4 },
    { "1110", 5 }, { "11110", 6 }, { "111110", 7 }, { "1111110", 8 },
};
static const struct { const char *code; int size; } dc_chroma[] = {
    { "00", 0 }, { "01", 1 }, { "10", 2 }, { "110", 3 }, { "1110", 4 },
    { "11110", 5 }, { "111110", 6 }, { "1111110", 7 }, { "11111110", 8 },
};

static int match_code(Bits *b, const char *code)
{
    int n = (int)strlen(code), i;
    for (i = 0; i < n; i++)
        if (bit_at(b, b->pos + i) != (code[i] == '1'))
            return 0;
    b->pos += n;
    return 1;
}

static int read_dc_v3(Bits *b, int chroma, int *prev)
{
    int i, size = -1, diff;
    if (chroma) {
        for (i = 0; i < 9 && size < 0; i++)
            if (match_code(b, dc_chroma[i].code))
                size = dc_chroma[i].size;
    } else {
        for (i = 0; i < 9 && size < 0; i++)
            if (match_code(b, dc_luma[i].code))
                size = dc_luma[i].size;
    }
    if (size < 0)
        return *prev;
    if (size == 0) {
        diff = 0;
    } else {
        unsigned v = get(b, size);
        diff = (v & (1u << (size - 1))) ? (int)v : (int)v - (1 << size) + 1;
    }
    *prev += diff * 4;
    return *prev;
}

/*
 * Returns 0 once the frame is decoded (the original returns non-zero while
 * more data is needed). Output: word 0 = number of halfwords, then the
 * halfwords - private to DecDCTin.
 */
int DecDCTvlc2(u_long *bs, u_long *buf, u_short *table)
{
    const uint8_t *hdr = (const uint8_t *)bs;
    unsigned words = hdr[0] | (hdr[1] << 8);
    unsigned qscale = hdr[4] | (hdr[5] << 8);
    unsigned version = hdr[6] | (hdr[7] << 8);
    uint16_t *out = (uint16_t *)(buf + 1);
    size_t target = (size_t)words * 2, n = 0;
    /* the dctin buffers are 0x17800 bytes */
    size_t limit = (0x17800 - 4) / 2;
    Bits b;
    int dc_prev[3] = { 0, 0, 0 };

    (void)table;
    if (!ac_ready)
        ac_init();
    if (target > limit)
        target = limit;

    b.data = hdr + 8;
    b.size = target * 2 + 64; /* the bitstream is never longer than its output */
    b.pos = 0;

    if (version != 2 && version != 3) {
        static int warned;
        if (!warned++)
            psx_log("MDEC: unsupported STR version %u\n", version);
        buf[0] = 0;
        return 0;
    }

    while (n + 6 * 64 < limit && n < target && !exhausted(&b)) {
        int blk;
        for (blk = 0; blk < 6; blk++) {
            int dc;
            if (version == 2) {
                dc = (int)get(&b, 10);
            } else {
                /* block order Cr, Cb, Y0..Y3 */
                int ch = blk < 2 ? blk + 1 : 0;
                dc = read_dc_v3(&b, blk < 2, &dc_prev[ch]) & 0x3FF;
            }
            out[n++] = (uint16_t)((qscale << 10) | (dc & 0x3FF));
            for (;;) {
                int found = 0;
                if (peek(&b, 2) == 2) {           /* "10": end of block */
                    b.pos += 2;
                    break;
                }
                if (peek(&b, 6) == 1) {           /* escape: 6-bit run, 10-bit level */
                    b.pos += 6;
                    out[n++] = (uint16_t)get(&b, 16);
                    continue;
                }
                {
                    unsigned w = peek(&b, 16);
                    if (ac_lut[w].len) {
                        int level = ac_lut[w].level;
                        b.pos += ac_lut[w].len;
                        if (get(&b, 1))
                            level = -level;
                        out[n++] = (uint16_t)((ac_lut[w].run << 10) | (level & 0x3FF));
                        found = 1;
                    }
                }
                if (!found || exhausted(&b))
                    break; /* corrupt stream: end the block */
            }
            out[n++] = 0xFE00;
        }
    }
    buf[0] = (u_long)n;
    return 0;
}

void DecDCTvlcBuild(u_short *table) { (void)table; }

/* ---- MDEC --------------------------------------------------------------------------- */

static const uint8_t zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* MPEG-1 default intra matrix (natural order); the MDEC uses 2 for DC */
static const uint8_t intra_matrix[64] = {
     8, 16, 19, 22, 26, 27, 29, 34,
    16, 16, 22, 24, 27, 29, 34, 37,
    19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48,
    26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69,
    27, 29, 35, 38, 46, 56, 69, 83,
};

static int   qt[64];            /* zigzag order */
static float idct_cos[8][8];
static int   mdec_ready;

static void mdec_init(void)
{
    int k, x, u;
    for (k = 0; k < 64; k++)
        qt[k] = intra_matrix[zigzag[k]];
    qt[0] = 2;
    for (x = 0; x < 8; x++)
        for (u = 0; u < 8; u++)
            idct_cos[x][u] = (float)((u == 0 ? sqrt(0.125) : 0.5) * cos((2 * x + 1) * u * M_PI / 16.0));
    mdec_ready = 1;
}

/* decode one block of run-level codes; returns codes consumed */
static size_t decode_block(const uint16_t *src, size_t avail, float out[64])
{
    int blk[64];
    size_t n = 0;
    int k = 0, q, i, x, y, u, v;
    uint16_t c;
    float tmp[64];

    memset(blk, 0, sizeof(blk));
    if (avail == 0)
        goto idct;
    /* skip padding */
    while (n < avail && src[n] == 0xFE00)
        n++;
    if (n >= avail)
        goto idct;
    c = src[n++];
    q = c >> 10;
    {
        int val = ((int16_t)(c << 6) >> 6) * qt[0];
        if (q == 0)
            val = ((int16_t)(c << 6) >> 6) * 2;
        if (val < -0x400) val = -0x400;
        if (val > 0x3FF) val = 0x3FF;
        blk[zigzag[0]] = val;
    }
    while (n < avail) {
        int val;
        c = src[n++];
        if (c == 0xFE00)
            break;
        k += (c >> 10) + 1;
        if (k > 63)
            break;
        val = (((int16_t)(c << 6) >> 6) * qt[k] * q + 4) / 8;
        if (q == 0)
            val = ((int16_t)(c << 6) >> 6) * 2;
        if (val < -0x400) val = -0x400;
        if (val > 0x3FF) val = 0x3FF;
        blk[zigzag[k]] = val;
    }

idct:
    /* separable 8x8 inverse DCT */
    for (y = 0; y < 8; y++)
        for (u = 0; u < 8; u++) {
            float s = 0;
            for (v = 0; v < 8; v++)
                s += idct_cos[y][v] * (float)blk[v * 8 + u];
            tmp[y * 8 + u] = s;
        }
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            float s = 0;
            for (i = 0; i < 8; i++)
                s += idct_cos[x][i] * tmp[y * 8 + i];
            out[y * 8 + x] = s;
        }
    return n;
}

#define MAX_MACROBLOCKS (40 * 32)

static uint16_t (*mb_pixels)[256];                /* 15-bit */
PSX_HEAP_BUFFER(mb_pixels, MAX_MACROBLOCKS * 256 * sizeof(uint16_t))
static int      mb_count;
static int      mb_out;           /* next macroblock to hand out */
static int      out_depth15 = 1;

static inline int clamp8(float v)
{
    int i = (int)lrintf(v);
    return i < -128 ? -128 : i > 127 ? 127 : i;
}

static void decode_macroblock(const float cr[64], const float cb[64], const float (*y)[64], uint16_t *dst)
{
    int px, py;
    for (py = 0; py < 16; py++)
        for (px = 0; px < 16; px++) {
            int yb = (py >> 3) * 2 + (px >> 3);
            float Y = y[yb][(py & 7) * 8 + (px & 7)];
            float R = cr[(py >> 1) * 8 + (px >> 1)];
            float B = cb[(py >> 1) * 8 + (px >> 1)];
            int r = clamp8(Y + 1.402f * R) + 128;
            int g = clamp8(Y - 0.3437f * B - 0.7143f * R) + 128;
            int b = clamp8(Y + 1.772f * B) + 128;
            dst[py * 16 + px] = (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
        }
}

static void (*out_callback)(void);
static u_long *out_buf;
static int     out_words;
static int     out_pending;
static int     serving;

static void serve(void)
{
    if (serving)
        return;
    serving = 1;
    while (out_pending && mb_out < mb_count) {
        int halfwords = out_words * 2, done = 0;
        uint16_t *dst = (uint16_t *)out_buf;
        out_pending = 0;
        while (done + 256 <= halfwords && mb_out < mb_count) {
            memcpy(dst + done, mb_pixels[mb_out++], 512);
            done += 256;
        }
        if (out_callback)
            out_callback();     /* may queue the next DecDCTout() */
    }
    serving = 0;
}

void DecDCTReset(int mode)
{
    (void)mode;
    if (!mdec_ready)
        mdec_init();
    mb_count = mb_out = 0;
    out_pending = 0;
}

void DecDCTin(u_long *buf, int mode)
{
    const uint16_t *src = (const uint16_t *)(buf + 1);
    size_t avail = buf[0], n = 0;
    float blocks[6][64];

    if (!mdec_ready)
        mdec_init();
    out_depth15 = (mode & 2) != 0;
    if (!out_depth15) {
        static int warned;
        if (!warned++)
            psx_log("MDEC: 24-bit output not implemented, using 15 bit\n");
    }
    psx_ws_movie_frame();
    mb_count = mb_out = 0;
    while (n < avail && mb_count < MAX_MACROBLOCKS) {
        int b;
        for (b = 0; b < 6; b++)
            n += decode_block(src + n, avail - n, blocks[b]);
        decode_macroblock(blocks[0], blocks[1], (const float (*)[64])&blocks[2], mb_pixels[mb_count++]);
    }
    serve();
}

void DecDCTout(u_long *buf, int size)
{
    out_buf = buf;
    out_words = size;
    out_pending = 1;
    serve();
}

int DecDCTinSync(int mode) { (void)mode; return 0; }
int DecDCToutSync(int mode) { (void)mode; return 0; }

int DecDCToutCallback(void (*func)(void))
{
    out_callback = func;
    return 0;
}

int DecDCTinCallback(void (*func)(void)) { (void)func; return 0; }
