/*
 * Movie playback support: CD streaming (libcd St*) and the MDEC (libpress).
 *
 * Not implemented yet: the stream never delivers a frame, so the game's
 * movie player gives up after its built-in timeout and continues.
 */
#include "psx.h"

typedef struct { u_char minute, second, sector, track; } CdlLOC;

int CdRead2(long mode) { (void)mode; return 1; }
int CdReady(int mode, u_char *result) { (void)mode; (void)result; return 0; }

CdlLOC *CdIntToPos(int i, CdlLOC *p)
{
    i += 150;
    p->sector = (u_char)(((i % 75) / 10) << 4 | ((i % 75) % 10));
    i /= 75;
    p->second = (u_char)(((i % 60) / 10) << 4 | ((i % 60) % 10));
    i /= 60;
    p->minute = (u_char)((i / 10) << 4 | (i % 10));
    return p;
}

void StSetRing(u_long *ring, u_long size) { (void)ring; (void)size; }
void StSetStream(u_long mode, u_long start, u_long end, void (*f1)(void), void (*f2)(void))
{
    (void)mode; (void)start; (void)end; (void)f1; (void)f2;
}
void StUnSetRing(void) {}
u_long StFreeRing(u_long *base) { (void)base; return 0; }

u_long StGetNext(u_long **addr, u_long **header)
{
    (void)addr; (void)header;
    psx_idle(); /* let time pass so the caller's timeout can expire */
    return 1;   /* no frame available */
}

static void (*dct_out_cb)(void);

void DecDCTReset(int mode) { (void)mode; }
void DecDCTvlcBuild(u_short *table) { (void)table; }
int  DecDCTvlc2(u_long *bs, u_long *buf, u_short *table) { (void)bs; (void)buf; (void)table; return 0; }
void DecDCTin(u_long *buf, int mode) { (void)buf; (void)mode; }
void DecDCTout(u_long *buf, int size) { (void)buf; (void)size; }
int  DecDCToutSync(int mode) { (void)mode; return 0; }

int DecDCToutCallback(void (*func)(void))
{
    dct_out_cb = func;
    return 0;
}

/* PC file server (development hardware only) */
int PCcreat(char *name, int perms) { (void)name; (void)perms; return -1; }
int PCwrite(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return -1; }
