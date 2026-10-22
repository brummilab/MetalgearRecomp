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
        last_vblank = psx_vblank_count();
        break;
    case CdlPause:
    case CdlStop:
    case CdlStandby:
        reading = 0;
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
void psx_cd_interrupt(void)
{
    uint32_t now = psx_vblank_count();
    int budget;

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
