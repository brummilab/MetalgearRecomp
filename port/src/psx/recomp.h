/*
 * Statically recompiled overlays (tools/recomp.py, runtime in recomp_rt.c).
 * The generated code carries its own copy of these types (see recomp.py).
 * No <stdint.h>: game code includes this next to the PsyQ headers, which
 * clash with it on MinGW; the port is 32-bit, so unsigned int is 32 bits.
 */
#ifndef PSX_RECOMP_H
#define PSX_RECOMP_H

typedef unsigned int (*RcFn)();
typedef struct { unsigned int addr; RcFn fn; } RcEntry;
typedef struct { unsigned int addr; void *value; } RcReloc;
typedef struct RcOverlay {
    const char    *name;
    unsigned int   base, size;      /* overlay image */
    unsigned int   text, text_end;
    const RcEntry *entries;
    int            n_entries;
    const RcReloc *relocs;
    int            n_relocs;
} RcOverlay;

/* make a recompiled overlay, already loaded to its address, callable */
void psx_recomp_install(const RcOverlay *ov);

#endif
