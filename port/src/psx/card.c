/*
 * Memory card emulation: BIOS card functions (_card_info, _card_load, ...)
 * and the "bu00:" file system (open/read/write/erase/firstfile/...).
 *
 * Each card is a standard 128 KiB raw image (.mcd), compatible with the
 * images used by PS1 emulators. Port 1 uses memcard1.mcd, port 2 is empty.
 * All operations complete immediately; the completion events are raised
 * synchronously, which the game's busy-wait loops depend on.
 */
#include "psx.h"

#include <stdio.h>
#include <string.h>

#define CARD_SIZE   (128 * 1024)
#define FRAME_SIZE  128
#define BLOCK_SIZE  8192
#define NUM_BLOCKS  16

/* event descriptors and specs (kernel.h) */
#define HwCARD     0xF0000011u
#define SwCARD     0xF4000001u
#define EvSpIOE    0x0004
#define EvSpERROR  0x8000
#define EvSpTIMOUT 0x0100
#define EvSpNEW    0x2000

#define FREAD   0x0001
#define FWRITE  0x0002
#define FCREAT  0x0200

typedef struct {
    int     present;
    int     dirty;
    char    path[512];
    uint8_t data[CARD_SIZE];
} Card;

static Card *cards;
PSX_HEAP_BUFFER(cards, 2 * sizeof(Card))
static char save_dir[480] = ".";

struct DIRENTRY {
    char  name[20];
    long  attr;
    long  size;
    struct DIRENTRY *next;
    long  head;
    char  system[4];
};

/* ---- image handling ------------------------------------------------------------- */

static uint8_t *frame(Card *c, int block, int f) { return &c->data[block * BLOCK_SIZE + f * FRAME_SIZE]; }

static void dir_checksum(uint8_t *e)
{
    uint8_t x = 0;
    int i;
    for (i = 0; i < 127; i++)
        x ^= e[i];
    e[127] = x;
}

static void card_format(Card *c)
{
    int i;
    memset(c->data, 0, CARD_SIZE);
    frame(c, 0, 0)[0] = 'M';
    frame(c, 0, 0)[1] = 'C';
    dir_checksum(frame(c, 0, 0));
    for (i = 1; i < 16; i++) {
        uint8_t *e = frame(c, 0, i);
        e[0] = 0xA0;
        e[8] = e[9] = 0xFF;
        dir_checksum(e);
    }
    for (i = 16; i < 36; i++) {
        uint8_t *e = frame(c, 0, i);
        memset(e, 0, FRAME_SIZE);
        e[0] = e[1] = e[2] = e[3] = 0xFF;
        e[8] = e[9] = 0xFF;
        dir_checksum(e);
    }
    frame(c, 0, 63)[0] = 'M';
    frame(c, 0, 63)[1] = 'C';
    dir_checksum(frame(c, 0, 63));
    c->dirty = 1;
}

static void card_flush(Card *c)
{
    FILE *f;
    if (!c->present || !c->dirty)
        return;
    f = fopen(c->path, "wb");
    if (!f) {
        psx_log("CARD: cannot write %s\n", c->path);
        return;
    }
    fwrite(c->data, 1, CARD_SIZE, f);
    fclose(f);
    c->dirty = 0;
}

void psx_card_init(const char *dir)
{
    FILE *f;
    Card *c = &cards[0];

    if (dir)
        snprintf(save_dir, sizeof(save_dir), "%s", dir);
    snprintf(c->path, sizeof(c->path), "%s/memcard1.mcd", save_dir);
    c->present = 1;
    f = fopen(c->path, "rb");
    if (f && fread(c->data, 1, CARD_SIZE, f) == CARD_SIZE) {
        psx_log("CARD: loaded %s\n", c->path);
    } else {
        psx_log("CARD: creating new card %s\n", c->path);
        card_format(c);
        card_flush(c);
    }
    if (f)
        fclose(f);
}

static Card *card_for_chan(long chan)
{
    int port = (int)((chan >> 4) & 1);
    if (chan & 0xF)
        return NULL; /* multitap slots */
    return cards[port].present ? &cards[port] : NULL;
}

static int formatted(Card *c) { return c->data[0] == 'M' && c->data[1] == 'C'; }

/* ---- BIOS card functions ------------------------------------------------------------ */

void InitCARD(long val) { (void)val; }
long StartCARD(void) { return 1; }
long StopCARD(void)  { return 1; }
void _bu_init(void)  {}

long _card_info(long chan)
{
    Card *c = card_for_chan(chan);
    psx_deliver_event(SwCARD, c ? EvSpIOE : EvSpTIMOUT);
    return 1;
}

long _card_clear(long chan)
{
    Card *c = card_for_chan(chan);
    psx_deliver_event(HwCARD, c ? EvSpIOE : EvSpTIMOUT);
    return 1;
}

long _card_load(long chan)
{
    Card *c = card_for_chan(chan);
    if (!c)
        psx_deliver_event(SwCARD, EvSpTIMOUT);
    else
        psx_deliver_event(SwCARD, formatted(c) ? EvSpIOE : EvSpNEW);
    return 1;
}

long _card_read(long chan, long block, unsigned char *buf)
{
    Card *c = card_for_chan(chan);
    if (!c || block < 0 || block >= CARD_SIZE / FRAME_SIZE) {
        psx_deliver_event(HwCARD, EvSpTIMOUT);
        return 0;
    }
    memcpy(buf, &c->data[block * FRAME_SIZE], FRAME_SIZE);
    psx_deliver_event(HwCARD, EvSpIOE);
    return 1;
}

long _card_write(long chan, long block, unsigned char *buf)
{
    Card *c = card_for_chan(chan);
    if (!c || block < 0 || block >= CARD_SIZE / FRAME_SIZE) {
        psx_deliver_event(HwCARD, EvSpTIMOUT);
        return 0;
    }
    memcpy(&c->data[block * FRAME_SIZE], buf, FRAME_SIZE);
    c->dirty = 1;
    card_flush(c);
    psx_deliver_event(HwCARD, EvSpIOE);
    return 1;
}

/* ---- file system --------------------------------------------------------------------- */

/* "bu00:NAME" -> card and file name */
static Card *parse_path(const char *path, const char **name)
{
    unsigned chan;
    if (strncmp(path, "bu", 2) != 0 || strlen(path) < 5 || path[4] != ':')
        return NULL;
    if (sscanf(path + 2, "%2x", &chan) != 1)
        return NULL;
    *name = path + 5;
    return card_for_chan((long)chan);
}

static uint8_t *dir_entry(Card *c, int block) { return frame(c, 0, block); }

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static int find_file(Card *c, const char *name)
{
    int b;
    for (b = 1; b < NUM_BLOCKS; b++) {
        uint8_t *e = dir_entry(c, b);
        if (e[0] == 0x51 && strncmp((char *)e + 10, name, 20) == 0)
            return b;
    }
    return -1;
}

/* data block of file block index i, starting at first block b */
static int chain_block(Card *c, int b, int i)
{
    while (i-- > 0 && b > 0) {
        uint8_t *e = dir_entry(c, b);
        int next = e[8] | (e[9] << 8);
        b = next == 0xFFFF ? -1 : next + 1;
    }
    return b;
}

static int match(const char *pat, const char *s)
{
    if (*pat == '*')
        return 1;
    while (*pat && *s) {
        if (*pat == '*')
            return 1;
        if (*pat != '?' && *pat != *s)
            return 0;
        pat++;
        s++;
    }
    return *pat == *s || *pat == '*';
}

typedef struct {
    int   used;
    Card *card;
    int   first;
    long  pos;
    long  size;
    int   mode;
} Fd;

static Fd fds[8];

static int create_file(Card *c, const char *name, int blocks)
{
    int found[NUM_BLOCKS], n = 0, b, i;
    for (b = 1; b < NUM_BLOCKS && n < blocks; b++)
        if ((dir_entry(c, b)[0] & 0xF0) == 0xA0)
            found[n++] = b;
    if (n < blocks)
        return -1;
    for (i = 0; i < blocks; i++) {
        uint8_t *e = dir_entry(c, found[i]);
        int next = (i + 1 < blocks) ? found[i + 1] - 1 : 0xFFFF;
        memset(e, 0, FRAME_SIZE);
        e[0] = (blocks == 1) ? 0x51 : (i == 0 ? 0x51 : (i + 1 == blocks ? 0x53 : 0x52));
        if (i == 0) {
            wr32(e + 4, (uint32_t)blocks * BLOCK_SIZE);
            strncpy((char *)e + 10, name, 20);
        }
        e[8] = (uint8_t)next;
        e[9] = (uint8_t)(next >> 8);
        dir_checksum(e);
        memset(&c->data[found[i] * BLOCK_SIZE], 0, BLOCK_SIZE);
    }
    c->dirty = 1;
    card_flush(c);
    return found[0];
}

int psx_open(const char *path, int flags)
{
    const char *name;
    Card *c = parse_path(path, &name);
    int b, i;

    if (!c || !formatted(c))
        return -1;
    b = find_file(c, name);
    if (flags & FCREAT) {
        if (b >= 0)
            return -1;
        b = create_file(c, name, (flags >> 16) ? (flags >> 16) : 1);
        if (b < 0)
            return -1;
    } else if (b < 0) {
        return -1;
    }
    for (i = 0; i < 8; i++) {
        if (!fds[i].used) {
            fds[i].used = 1;
            fds[i].card = c;
            fds[i].first = b;
            fds[i].pos = 0;
            fds[i].size = (long)rd32(dir_entry(c, b) + 4);
            fds[i].mode = flags;
            return i + 2; /* the BIOS keeps 0/1 for the console */
        }
    }
    return -1;
}

static Fd *fd_get(int fd)
{
    fd -= 2;
    return (fd >= 0 && fd < 8 && fds[fd].used) ? &fds[fd] : NULL;
}

static long file_io(int fd, void *buf, long n, int write)
{
    Fd *f = fd_get(fd);
    long done = 0;
    if (!f)
        return -1;
    n &= ~(long)(FRAME_SIZE - 1);
    while (done < n && f->pos < f->size) {
        int blk = chain_block(f->card, f->first, (int)(f->pos / BLOCK_SIZE));
        long off = f->pos % BLOCK_SIZE, chunk = FRAME_SIZE;
        uint8_t *p;
        if (blk < 0)
            break;
        p = &f->card->data[blk * BLOCK_SIZE + off];
        if (write)
            memcpy(p, (uint8_t *)buf + done, (size_t)chunk);
        else
            memcpy((uint8_t *)buf + done, p, (size_t)chunk);
        f->pos += chunk;
        done += chunk;
        psx_deliver_event(HwCARD, EvSpIOE);   /* one per 128-byte sector */
    }
    if (write) {
        f->card->dirty = 1;
        card_flush(f->card);
    }
    psx_deliver_event(SwCARD, EvSpIOE);
    return done;
}

int psx_read(int fd, void *buf, long n) { return (int)file_io(fd, buf, n, 0); }
int psx_write(int fd, void *buf, long n) { return (int)file_io(fd, buf, n, 1); }

long psx_lseek(int fd, long offset, int whence)
{
    Fd *f = fd_get(fd);
    if (!f)
        return -1;
    if (whence == 0)
        f->pos = offset;
    else if (whence == 1)
        f->pos += offset;
    else
        f->pos = f->size + offset;
    return f->pos;
}

int psx_close(int fd)
{
    Fd *f = fd_get(fd);
    if (!f)
        return -1;
    f->used = 0;
    return fd;
}

long erase(char *path)
{
    const char *name;
    Card *c = parse_path(path, &name);
    int b;
    if (!c || (b = find_file(c, name)) < 0)
        return 0;
    while (b > 0) {
        uint8_t *e = dir_entry(c, b);
        int next = e[8] | (e[9] << 8);
        e[0] = (uint8_t)(0xA0 | (e[0] & 0x0F));
        dir_checksum(e);
        b = next == 0xFFFF ? -1 : next + 1;
    }
    c->dirty = 1;
    card_flush(c);
    return 1;
}

long format(char *path)
{
    const char *name;
    Card *c = parse_path(path, &name);
    if (!c)
        return 0;
    card_format(c);
    card_flush(c);
    return 1;
}

static int dir_scan_card;
static int dir_scan_block;
static char dir_scan_pattern[24];

static struct DIRENTRY *scan(struct DIRENTRY *d)
{
    Card *c = &cards[dir_scan_card];
    for (; dir_scan_block < NUM_BLOCKS; dir_scan_block++) {
        uint8_t *e = dir_entry(c, dir_scan_block);
        if (e[0] == 0x51 && match(dir_scan_pattern, (char *)e + 10)) {
            memset(d, 0, sizeof(*d));
            memcpy(d->name, e + 10, 20);
            d->size = (long)rd32(e + 4);
            d->head = dir_scan_block;
            dir_scan_block++;
            return d;
        }
    }
    return NULL;
}

struct DIRENTRY *firstfile(char *path, struct DIRENTRY *d)
{
    const char *name;
    Card *c = parse_path(path, &name);
    if (!c || !formatted(c))
        return NULL;
    dir_scan_card = (int)(c - cards);
    dir_scan_block = 1;
    snprintf(dir_scan_pattern, sizeof(dir_scan_pattern), "%s", name);
    return scan(d);
}

struct DIRENTRY *nextfile(struct DIRENTRY *d) { return scan(d); }
