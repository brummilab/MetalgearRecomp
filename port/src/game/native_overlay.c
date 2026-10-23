/*
 * Stage overlays in the native build.
 *
 * The game still loads "<stage>.bin" from the disc into the overlay area,
 * but that is MIPS code. Once it is loaded, the overlay's chara table (the
 * first thing in every overlay) is replaced with the table of the natively
 * compiled overlay, so GM_GetCharaID() finds native constructors.
 *
 * Overlays that are not fully decompiled yet are recompiled from their
 * original binary (tools/recomp.py). Their image stays where the game loaded
 * it, chara table included; installing them writes jumps to the recompiled
 * functions into the code area (psx_recomp_install).
 *
 * Compiled as game code (PsyQ headers).
 */
#include <stdio.h>
#include <string.h>
#include "mgstype.h"
#include "libgv/libgv.h"
#include "game/game.h"
#include "charadef.h"
#include "native_overlay.h"
#include "../psx/recomp.h"

extern void *StageCharacterEntries;

int mgs_stage_bin_id = -1;

/* diagnostics: type and size of the first records taken from the CD stream,
 * with the first words of their data */
void mgs_trace_stream(int type, int size, const void *rec)
{
    static int n;
    const unsigned int *w = (const unsigned int *)rec;
    if (n++ >= 40)
        return;
    printf("stream: #%d type %02x size %d at %p: %08x %08x %08x %08x\n", n, type, size, rec,
           w[0], w[1], w[2], w[3]);
}

/* end of a stage load: a stage binary read straight from disc to the
 * overlay address (streamed region) gets the same treatment as one copied
 * there by GM_LoadInitBin() */
void mgs_native_stage_loaded(void)
{
    if (mgs_stage_bin_id >= 0) {
        int id = mgs_stage_bin_id;
        mgs_stage_bin_id = -1;
        mgs_native_overlay_loaded(id);
    }
}

int mgs_native_overlay_available(const char *name)
{
    const NativeOverlay *ov;
    const RecompOverlay *rc;
    for (ov = mgs_native_overlays; ov->name; ov++)
        if (strcmp(ov->name, name) == 0)
            return 1;
    for (rc = mgs_recomp_overlays; rc->name; rc++)
        if (strcmp(rc->name, name) == 0)
            return 1;
    return 0;
}

void mgs_native_overlay_loaded(int id)
{
    const NativeOverlay *ov;
    const RecompOverlay *rc;
    CHARA *dst = (CHARA *)StageCharacterEntries;
    int n;

    for (ov = mgs_native_overlays; ov->name; ov++) {
        if ((GV_StrCode((char *)ov->name) & 0xFFFF) != (id & 0xFFFF))
            continue;
        for (n = 0; ov->charas[n].func; n++)
            ;
        memcpy(dst, ov->charas, (n + 1) * sizeof(CHARA));
        printf("native overlay %s: %d charas\n", ov->name, n);
        return;
    }
    for (rc = mgs_recomp_overlays; rc->name; rc++) {
        if ((GV_StrCode((char *)rc->name) & 0xFFFF) != (id & 0xFFFF))
            continue;
        if ((unsigned int)dst != rc->ov->base) {
            printf("recompiled overlay %s: loaded to %p instead of 0x%08x\n", rc->name, dst, rc->ov->base);
            break;
        }
        psx_recomp_install(rc->ov);
        return;
    }
    printf("native overlay %04x: not available (not fully decompiled yet)\n", id & 0xFFFF);
    memset(dst, 0, sizeof(CHARA));
}
