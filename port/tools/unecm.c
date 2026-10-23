/*
 * unecm: restores a CD image from the ECM format (Neill Corlett's
 * "Error Code Modeler"), which drops the sync, EDC and ECC fields that can
 * be recomputed from the user data. Disc images are often stored this way
 * (*.bin.ecm); the port needs the plain image.
 *
 *   unecm <image.bin.ecm> [image.bin]
 *
 * The whole output is checked against the checksum at the end of the file.
 * Afterwards the image is checked for the disc the port expects
 * (Metal Gear Solid Integral disc 1, SLPM-86247).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t  ecc_f[256], ecc_b[256];
static uint32_t edc_lut[256];

static void tables_init(void)
{
    int i, k;
    for (i = 0; i < 256; i++) {
        int j = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
        uint32_t edc = (uint32_t)i;
        ecc_f[i] = (uint8_t)j;
        ecc_b[i ^ j] = (uint8_t)i;
        for (k = 0; k < 8; k++)
            edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001u : 0);
        edc_lut[i] = edc;
    }
}

static uint32_t edc(uint32_t e, const uint8_t *p, size_t n)
{
    while (n--)
        e = (e >> 8) ^ edc_lut[(e ^ *p++) & 0xFF];
    return e;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Reed-Solomon P or Q parity over the 4 header bytes plus the data */
static void ecc_pq(const uint8_t *hdr, const uint8_t *data, int major_count, int minor_count,
                   int major_mult, int minor_inc, uint8_t *out)
{
    int size = major_count * minor_count, major, minor;
    for (major = 0; major < major_count; major++) {
        int index = (major >> 1) * major_mult + (major & 1);
        uint8_t a = 0, b = 0;
        for (minor = 0; minor < minor_count; minor++) {
            uint8_t t = index < 4 ? hdr[index] : data[index - 4];
            index += minor_inc;
            if (index >= size)
                index -= size;
            a ^= t;
            b ^= t;
            a = ecc_f[a];
        }
        a = ecc_b[ecc_f[a] ^ b];
        out[major] = a;
        out[major + major_count] = a ^ b;
    }
}

static void ecc_sector(const uint8_t *hdr, const uint8_t *data, uint8_t *ecc)
{
    ecc_pq(hdr, data, 86, 24, 2, 86, ecc);          /* P */
    ecc_pq(hdr, data, 52, 43, 86, 88, ecc + 0xAC);  /* Q */
}

/* fill in everything the ECM file left out (s = full 2352-byte sector) */
static void rebuild(uint8_t *s, int type)
{
    static const uint8_t zero[4];
    memset(s, 0xFF, 12);
    s[0] = s[11] = 0;
    if (type == 1) {
        s[0x0F] = 1;
        memset(s + 0x814, 0, 8);
        put32(s + 0x810, edc(0, s, 0x810));
        ecc_sector(s + 0x0C, s + 0x10, s + 0x81C);
    } else {
        s[0x0F] = 2;
        memcpy(s + 0x10, s + 0x14, 4);              /* subheader copy */
        if (type == 2) {
            put32(s + 0x818, edc(0, s + 0x10, 0x808));
            ecc_sector(zero, s + 0x10, s + 0x81C);
        } else {
            put32(s + 0x92C, edc(0, s + 0x10, 0x91C));
        }
    }
}

static int fail(const char *msg, const char *path)
{
    fprintf(stderr, "unecm: %s%s%s\n", msg, path ? ": " : "", path ? path : "");
    return 1;
}

/* look for the boot file name in SYSTEM.CNF (somewhere in the first sectors) */
static void identify(const char *path)
{
    static uint8_t buf[2352 * 64];
    FILE *f = fopen(path, "rb");
    size_t n, i;
    long long len;

    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    printf("image size: %lld bytes (%s multiple of 2352)\n", len, len % 2352 ? "NOT a" : "a");
    for (i = 0; i + 11 <= n; i++) {
        if (memcmp(buf + i, "SLPM_862.4", 10) == 0 || memcmp(buf + i, "SLUS_005.9", 10) == 0 ||
            memcmp(buf + i, "SLES_", 5) == 0) {
            printf("boot file: %.11s", (const char *)buf + i);
            if (memcmp(buf + i, "SLPM_862.47", 11) == 0)
                printf("  -> Metal Gear Solid Integral disc 1: the right disc\n");
            else
                printf("  -> NOT disc 1 of Integral (JP); the port needs SLPM_862.47\n");
            return;
        }
    }
    printf("boot file: not found (not a PlayStation disc?)\n");
}

int main(int argc, char **argv)
{
    static uint8_t s[2352];
    char outname[4096];
    const char *in_path, *out_path;
    FILE *in, *out;
    uint32_t check = 0, stored;
    uint8_t magic[4], tail[4];
    long long sectors = 0;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: unecm <image.bin.ecm> [image.bin]\n");
        return 2;
    }
    in_path = argv[1];
    if (argc == 3) {
        out_path = argv[2];
    } else {
        size_t n = strlen(in_path);
        if (n > 4 && strcmp(in_path + n - 4, ".ecm") == 0 && n - 4 < sizeof(outname)) {
            memcpy(outname, in_path, n - 4);
            outname[n - 4] = 0;
        } else {
            snprintf(outname, sizeof(outname), "%s.bin", in_path);
        }
        out_path = outname;
    }
    tables_init();

    in = fopen(in_path, "rb");
    if (!in)
        return fail("cannot open", in_path);
    if (fread(magic, 1, 4, in) != 4 || memcmp(magic, "ECM", 4) != 0)
        return fail("not an ECM file", in_path);
    out = fopen(out_path, "rb");
    if (out) {
        fclose(out);
        return fail("output exists, not overwriting", out_path);
    }
    out = fopen(out_path, "wb");
    if (!out)
        return fail("cannot create", out_path);
    printf("decoding %s\n      -> %s\n", in_path, out_path);

    for (;;) {
        int c = fgetc(in), bits = 5, type;
        uint32_t num;
        if (c == EOF)
            return fail("truncated file", in_path);
        type = c & 3;
        num = (uint32_t)(c >> 2) & 0x1F;
        while (c & 0x80) {
            c = fgetc(in);
            if (c == EOF)
                return fail("truncated file", in_path);
            if (bits > 31 || (uint32_t)(c & 0x7F) >= (0x80000000u >> (bits - 1)))
                return fail("corrupt sector count", in_path);
            num |= (uint32_t)(c & 0x7F) << bits;
            bits += 7;
        }
        if (num == 0xFFFFFFFFu)
            break;
        num++;
        if (type == 0) {
            while (num) {
                uint32_t b = num > sizeof(s) ? (uint32_t)sizeof(s) : num;
                if (fread(s, 1, b, in) != b)
                    return fail("truncated file", in_path);
                check = edc(check, s, b);
                if (fwrite(s, 1, b, out) != b)
                    return fail("write error", out_path);
                num -= b;
            }
            continue;
        }
        for (; num; num--) {
            const uint8_t *w = s;
            size_t wn = 2352;
            if (type == 1) {
                if (fread(s + 0x0C, 1, 3, in) != 3 || fread(s + 0x10, 1, 0x800, in) != 0x800)
                    return fail("truncated file", in_path);
            } else {
                size_t rn = type == 2 ? 0x804 : 0x918;
                if (fread(s + 0x14, 1, rn, in) != rn)
                    return fail("truncated file", in_path);
                w = s + 0x10;   /* mode 2 records hold 2336 bytes (no sync/header) */
                wn = 2336;
            }
            rebuild(s, type);
            check = edc(check, w, wn);
            if (fwrite(w, 1, wn, out) != wn)
                return fail("write error", out_path);
            if ((++sectors & 0x3FFF) == 0) {
                printf("\r%lld sectors", sectors);
                fflush(stdout);
            }
        }
    }
    if (fread(tail, 1, 4, in) != 4)
        return fail("checksum missing", in_path);
    fclose(in);
    if (fclose(out) != 0)
        return fail("write error", out_path);
    stored = (uint32_t)tail[0] | (uint32_t)tail[1] << 8 | (uint32_t)tail[2] << 16 | (uint32_t)tail[3] << 24;
    printf("\r%lld sectors rebuilt\n", sectors);
    if (stored != check) {
        fprintf(stderr, "unecm: CHECKSUM ERROR (0x%08X, expected 0x%08X): the image is damaged\n",
                (unsigned)check, (unsigned)stored);
        return 1;
    }
    printf("checksum OK\n");
    identify(out_path);
    return 0;
}
