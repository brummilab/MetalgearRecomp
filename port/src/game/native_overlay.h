#ifndef NATIVE_OVERLAY_H
#define NATIVE_OVERLAY_H

typedef struct {
    const char *name;
    CHARA      *charas;
} NativeOverlay;

extern const NativeOverlay mgs_native_overlays[];

/* overlays that still contain assembly, recompiled from the original binary
 * (tools/recomp.py); empty without the PS1 build */
typedef struct {
    const char             *name;
    const struct RcOverlay *ov;
} RecompOverlay;

extern const RecompOverlay mgs_recomp_overlays[];

/* called by GM_LoadInitBin() after a stage binary was read from disc */
void mgs_native_overlay_loaded(int id);

#endif
