#ifndef NATIVE_OVERLAY_H
#define NATIVE_OVERLAY_H

typedef struct {
    const char *name;
    CHARA      *charas;
} NativeOverlay;

extern const NativeOverlay mgs_native_overlays[];

/* called by GM_LoadInitBin() after a stage binary was read from disc */
void mgs_native_overlay_loaded(int id);

#endif
