/*
 * libcd replacement reading from a disc image of the user's own copy.
 *
 * Supported images: raw .bin (2352 bytes/sector, e.g. from a .cue/.bin dump)
 * and cooked .iso (2048 bytes/sector). The game reads with CdlReadN and pulls
 * each sector in its "data ready" callback with CdGetSector(): first the
 * 4-byte header plus 8-byte XA subheader, then the 2048 data bytes - the
 * layout of CdlModeSize1 mode on the console.
 */
#include "psx.h"

#include <stdio.h>
#include <string.h>

#define RAW_SECTOR   2352
#define ISO_SECTOR   2048
#define SECTOR_BYTES 2340        /* header + subheader + data + EDC/ECC */
#define PREGAP       150         /* LBA 0 = MSF 00:02:00 */
#define SECTORS_PER_VBLANK 10    /* ~8x drive speed */

typedef void (*CdlCB)(u_char status, u_char *result);

static FILE    *image;
static int      raw;             /* 2352-byte sectors */
static long     image_sectors;

static int      pos;             /* next LBA to read */
static int      reading;
static int      streaming;     /* CdRead2 stream mode */
static u_char   mode;
static u_char   last_result[8];
static CdlCB    ready_cb;
static CdlCB    sync_cb;

static u_char   sector[SECTOR_BYTES];
static int      sector_pos;      /* read offset for CdGetSector */
static uint32_t last_vblank;

static int bcd2i(u_char b) { return (b >> 4) * 10 + (b & 0xF); }
static u_char i2bcd(int i) { return (u_char)(((i / 10) << 4) | (i % 10)); }

void psx_cd_init(const char *path)
{
    u_char sync[12];
    static const u_char sync_pattern[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    long size;

    if (!path) {
        psx_log("CD: no disc image given (--iso), the game will not find its data\n");
        return;
    }
    image = fopen(path, "rb");
    if (!image) {
        psx_fatal("CD: cannot open disc image %s", path);
        return;
    }
    fseek(image, 0, SEEK_END);
    size = ftell(image);
    fseek(image, 0, SEEK_SET);
    raw = fread(sync, 1, 12, image) == 12 && memcmp(sync, sync_pattern, 12) == 0;
    image_sectors = size / (raw ? RAW_SECTOR : ISO_SECTOR);
    psx_log("CD: %s, %ld sectors (%s)\n", path, image_sectors, raw ? "raw 2352" : "iso 2048");
}

static int load_sector(int lba)
{
    int msf = lba + PREGAP;
    memset(sector, 0, sizeof(sector));
    sector_pos = 0;
    if (!image || lba < 0 || lba >= image_sectors)
        return 0;
    if (raw) {
        /* skip the 12 sync bytes */
        fseek(image, (long)lba * RAW_SECTOR + 12, SEEK_SET);
        if (fread(sector, 1, SECTOR_BYTES, image) != SECTOR_BYTES)
            return 0;
    } else {
        sector[0] = i2bcd(msf / 75 / 60);
        sector[1] = i2bcd((msf / 75) % 60);
        sector[2] = i2bcd(msf % 75);
        sector[3] = 2;                       /* mode 2 */
        sector[6] = sector[10] = 0x08;       /* subheader: form 1 data */
        fseek(image, (long)lba * ISO_SECTOR, SEEK_SET);
        if (fread(sector + 12, 1, ISO_SECTOR, image) != ISO_SECTOR)
            return 0;
    }
    return 1;
}

int CdInit(void)
{
    reading = 0;
    ready_cb = sync_cb = NULL;
    return 1;
}

int CdReset(int m)
{
    (void)m;
    reading = 0;
    return 1;
}

void CdFlush(void) {}

CdlCB CdReadyCallback(CdlCB func)
{
    CdlCB old = ready_cb;
    ready_cb = func;
    return old;
}

CdlCB CdSyncCallback(CdlCB func)
{
    CdlCB old = sync_cb;
    sync_cb = func;
    return old;
}

static void set_loc(const u_char *p)
{
    pos = (bcd2i(p[0]) * 60 + bcd2i(p[1])) * 75 + bcd2i(p[2]) - PREGAP;
}

/* command numbers from libcd.h */
enum {
    CdlNop = 0x01, CdlSetloc = 0x02, CdlPlay = 0x03, CdlForward = 0x04, CdlBackward = 0x05,
    CdlReadN = 0x06, CdlStandby = 0x07, CdlStop = 0x08, CdlPause = 0x09, CdlMute = 0x0B,
    CdlDemute = 0x0C, CdlSetfilter = 0x0D, CdlSetmode = 0x0E, CdlGetparam = 0x0F,
    CdlGetlocL = 0x10, CdlGetlocP = 0x11, CdlSeekL = 0x15, CdlSeekP = 0x16, CdlReadS = 0x1B,
};

int CdControl(u_char com, u_char *param, u_char *result)
{
    u_char status = 0x02 | (reading ? 0x20 : 0); /* motor on, reading */

    switch (com) {
    case CdlSetloc:
    case CdlSeekL:
    case CdlSeekP:
        if (param)
            set_loc(param);
        break;
    case CdlReadN:
    case CdlReadS:
        if (param)
            set_loc(param);
        reading = 1;
        streaming = 0;
        last_vblank = psx_vblank_count();
        break;
    case CdlPause:
    case CdlStop:
    case CdlStandby:
        reading = 0;
        streaming = 0;
        break;
    case CdlSetmode:
        if (param)
            mode = param[0];
        break;
    case CdlGetparam:
        if (result) {
            result[1] = mode;
        }
        break;
    case CdlGetlocL:
        if (result)
            memcpy(result, sector, 8);
        break;
    case CdlGetlocP: {
        int msf = pos + PREGAP;
        if (result) {
            memset(result, 0, 8);
            result[0] = 1;
            result[1] = 1;
            result[5] = i2bcd(msf / 75 / 60);
            result[6] = i2bcd((msf / 75) % 60);
            result[7] = i2bcd(msf % 75);
        }
        break;
    }
    default:
        break;
    }
    if (result && com != CdlGetlocL && com != CdlGetlocP)
        result[0] = status;
    memset(last_result, 0, sizeof(last_result));
    last_result[0] = status;
    return 1;
}

int CdControlB(u_char com, u_char *param, u_char *result) { return CdControl(com, param, result); }

int CdSync(int m, u_char *result)
{
    (void)m;
    if (result)
        memcpy(result, last_result, 8);
    return 2; /* CdlComplete */
}

int CdGetSector(void *madr, int size)
{
    int bytes = size * 4;
    if (sector_pos + bytes > SECTOR_BYTES)
        bytes = SECTOR_BYTES - sector_pos;
    if (bytes > 0) {
        memcpy(madr, sector + sector_pos, (size_t)bytes);
        sector_pos += bytes;
    }
    return 1;
}

/* called from the interrupt dispatcher: deliver the next sectors */
static void stream_interrupt(void);

void psx_cd_interrupt(void)
{
    uint32_t now = psx_vblank_count();
    int budget;

    if (streaming && reading) {
        stream_interrupt();
        return;
    }

    if (!reading || !ready_cb)
        return;
    if (now == last_vblank)
        return;
    budget = (int)(now - last_vblank) * SECTORS_PER_VBLANK;
    if (budget > 4 * SECTORS_PER_VBLANK)
        budget = 4 * SECTORS_PER_VBLANK;
    last_vblank = now;

    while (budget-- > 0 && reading && ready_cb) {
        u_char res[8] = { 0x22 };
        if (!load_sector(pos)) {
            ready_cb(5 /* CdlDiskError */, res);
            reading = 0;
            break;
        }
        pos++;
        ready_cb(1 /* CdlDataReady */, res);
    }
}

/* ---- streaming (CdRead2 + libcd St* functions) ------------------------------------- */

/*
 * Movies are read with CdRead2(CdlModeStream2 | CdlModeSpeed | CdlModeRT):
 * the drive delivers sectors at its real data rate. STR sectors (data
 * starting with the 0x0160 magic and a 32-byte StHEADER) are assembled into
 * frames, XA audio sectors are decoded and fed to the SPU. The game polls
 * finished frames with StGetNext() and releases them with StFreeRing().
 */

void psx_xa_decode_sector(const uint8_t *subheader, const uint8_t *data);
void psx_xa_reset(void);
void psx_spu_xa_flush(void);

#define STR_MAGIC       0x0160
#define STR_HEADER      32
#define STR_PAYLOAD     2016
#define STR_MAX_SECTORS 40
#define STR_SLOTS       6

enum { SLOT_FREE, SLOT_FILLING, SLOT_READY, SLOT_LOCKED };

typedef struct {
    int      state;
    uint32_t frame;          /* StHEADER.frameCount */
    unsigned seq;            /* delivery order */
    int      sectors, received;
    uint8_t  header[STR_HEADER];
    uint8_t  data[STR_MAX_SECTORS * STR_PAYLOAD];
} StrSlot;

static StrSlot  slots[STR_SLOTS];
static unsigned slot_seq;
static long     stream_mode;
static double   stream_budget;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32le(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void stream_reset(void)
{
    int i;
    for (i = 0; i < STR_SLOTS; i++)
        slots[i].state = SLOT_FREE;
    slot_seq = 0;
}

static void stream_video_sector(const uint8_t *d)
{
    uint16_t sec = rd16(d + 4), nsec = rd16(d + 6);
    uint32_t frame = rd32le(d + 8);
    StrSlot *s = NULL;
    int i;

    if (nsec == 0 || nsec > STR_MAX_SECTORS || sec >= nsec)
        return;
    for (i = 0; i < STR_SLOTS; i++)
        if (slots[i].state == SLOT_FILLING && slots[i].frame == frame)
            s = &slots[i];
    if (!s) {
        for (i = 0; i < STR_SLOTS && !s; i++)
            if (slots[i].state == SLOT_FREE)
                s = &slots[i];
        if (!s)
            return;     /* ring full: the frame is dropped, as on the console */
        s->state = SLOT_FILLING;
        s->frame = frame;
        s->sectors = nsec;
        s->received = 0;
        memcpy(s->header, d, STR_HEADER);
    }
    memcpy(s->data + (size_t)sec * STR_PAYLOAD, d + STR_HEADER, STR_PAYLOAD);
    if (++s->received >= s->sectors) {
        s->state = SLOT_READY;
        s->seq = slot_seq++;
    }
}

static void stream_sector(void)
{
    const uint8_t *sub = sector + 4;   /* file, channel, submode, coding */
    if (raw && (sub[2] & 0x04)) {
        if (stream_mode & 0x40 /* CdlModeRT */)
            psx_xa_decode_sector(sub, sector + 12);
        return;
    }
    if (rd16(sector + 12) == STR_MAGIC)
        stream_video_sector(sector + 12);
}

/* called from psx_cd_interrupt() while streaming */
static void stream_interrupt(void)
{
    uint32_t now = psx_vblank_count();
    double rate = (stream_mode & 0x80) ? 150.0 : 75.0;   /* sectors per second */

    if (now == last_vblank)
        return;
    stream_budget += (now - last_vblank) * rate / 59.94;
    if (stream_budget > 30)
        stream_budget = 30;
    last_vblank = now;
    while (stream_budget >= 1.0 && streaming) {
        stream_budget -= 1.0;
        if (!load_sector(pos)) {
            streaming = 0;
            reading = 0;
            break;
        }
        pos++;
        stream_sector();
    }
}

int CdRead2(long m)
{
    stream_mode = m;
    streaming = 1;
    reading = 1;
    stream_budget = 0;
    last_vblank = psx_vblank_count();
    psx_xa_reset();
    return 1;
}

int CdReady(int m, u_char *result)
{
    (void)m;
    if (result)
        memcpy(result, last_result, 8);
    return 0; /* CdlNoIntr */
}

typedef struct { u_char minute, second, sector, track; } CdlLOC;

CdlLOC *CdIntToPos(int i, CdlLOC *p)
{
    i += PREGAP;
    p->sector = i2bcd(i % 75);
    p->second = i2bcd((i / 75) % 60);
    p->minute = i2bcd(i / 75 / 60);
    p->track = 0;
    return p;
}

int CdPosToInt(CdlLOC *p)
{
    return (bcd2i(p->minute) * 60 + bcd2i(p->second)) * 75 + bcd2i(p->sector) - PREGAP;
}

void StSetRing(u_long *ring, u_long size)
{
    (void)ring; (void)size; /* frames are kept in the slots above */
    stream_reset();
}

void StSetStream(u_long mode, u_long start, u_long end, void (*f1)(void), void (*f2)(void))
{
    (void)mode; (void)start; (void)end; (void)f1; (void)f2;
    stream_reset();
}

void StUnSetRing(void)
{
    streaming = 0;
    stream_reset();
    psx_spu_xa_flush();
}

void StClearRing(void) { stream_reset(); }

u_long StGetNext(u_long **addr, u_long **header)
{
    StrSlot *best = NULL;
    int i;

    for (i = 0; i < STR_SLOTS; i++)
        if (slots[i].state == SLOT_READY && (!best || slots[i].seq < best->seq))
            best = &slots[i];
    if (!best) {
        psx_idle(); /* let the drive deliver more sectors */
        return 1;
    }
    best->state = SLOT_LOCKED;
    *addr = (u_long *)best->data;
    *header = (u_long *)best->header;
    return 0;
}

u_long StFreeRing(u_long *base)
{
    int i;
    for (i = 0; i < STR_SLOTS; i++) {
        if ((u_long *)slots[i].data == base) {
            slots[i].state = SLOT_FREE;
            return 0;
        }
    }
    return 1;
}
