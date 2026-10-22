/*
 * Stage overlays in the native build.
 *
 * The game still loads "<stage>.bin" from the disc into the overlay area,
 * but that is MIPS code. Once it is loaded, the overlay's chara table (the
 * first thing in every overlay) is replaced with the table of the natively
 * compiled overlay, so GM_GetCharaID() finds native constructors.
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

extern void *StageCharacterEntries;

int mgs_native_overlay_available(const char *name)
{
    const NativeOverlay *ov;
    for (ov = mgs_native_overlays; ov->name; ov++)
        if (strcmp(ov->name, name) == 0)
            return 1;
    return 0;
}

void mgs_native_overlay_loaded(int id)
{
    const NativeOverlay *ov;
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
    printf("native overlay %04x: not available (not fully decompiled yet)\n", id & 0xFFFF);
    memset(dst, 0, sizeof(CHARA));
}
