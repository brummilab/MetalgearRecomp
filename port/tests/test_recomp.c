/*
 * Static recompiler check: functions of the original executable, recompiled
 * from the PS1 build by tools/recomp.py, against the natively compiled
 * decompiled source of the same functions, on random inputs.
 *
 * Built only when the PS1 matching build is present (obj/_mgsi.exe).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx/psx.h"
#include "psx/recomp.h"

typedef struct { const char *name; const RcOverlay *ov; } RecompOverlay;
extern const RecompOverlay mgs_recomp_overlays[];
int psx_mem_init(void);

typedef struct { short vx, vy, vz, pad; } SV;

/* native (decompiled source) */
int  GV_StrCode(const char *s);
void GV_AddVec3(SV *a, SV *b, SV *c);
void GV_SubVec3(SV *a, SV *b, SV *c);
int  GV_VecLen3(SV *v);
int  GV_DiffVec3(SV *a, SV *b);
int  GV_VecDir2(SV *v);
int  GV_DiffDirU(int a, int b);
int  GV_DiffDirS(int a, int b);
int  GV_DiffDirAbs(int a, int b);
int  GV_NearExp2(int a, int b);
int  GV_NearExp4(int a, int b);

/* recompiled from the original machine code */
#define RC(name) uint32_t rc_exe_fn_##name(uint32_t, uint32_t, uint32_t, uint32_t);
RC(GV_StrCode) RC(GV_AddVec3) RC(GV_SubVec3) RC(GV_VecLen3) RC(GV_DiffVec3) RC(GV_VecDir2)
RC(GV_DiffDirU) RC(GV_DiffDirS) RC(GV_DiffDirAbs) RC(GV_NearExp2) RC(GV_NearExp4)

#define P(x) ((uint32_t)(uintptr_t)(x))

static int failures;

static void check(const char *what, int i, long native, long recomp)
{
    if (native != recomp && failures++ < 20)
        printf("FAIL %s #%d: native %ld, recompiled %ld\n", what, i, native, recomp);
}

static short rnd16(void)
{
    return (short)(rand() & 0xFFFF) >> (rand() % 6);
}

static void rnd_vec(SV *v)
{
    v->vx = rnd16(); v->vy = rnd16(); v->vz = rnd16(); v->pad = rnd16();
}

int main(void)
{
    static const char *names[] = { "brf", "title", "s00a", "d01a", "init", "", "snake", "SOCOM" };
    int i;

    srand(1998);
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
        check("GV_StrCode", i, GV_StrCode(names[i]), (int)rc_exe_fn_GV_StrCode(P(names[i]), 0, 0, 0));

    for (i = 0; i < 2000; i++) {
        SV a, b, c1, c2;
        int x = rand() % 8192 - 2048, y = rand() % 8192 - 2048;

        rnd_vec(&a); rnd_vec(&b);
        memset(&c1, 0, sizeof(c1)); memset(&c2, 0, sizeof(c2));
        GV_AddVec3(&a, &b, &c1);
        rc_exe_fn_GV_AddVec3(P(&a), P(&b), P(&c2), 0);
        check("GV_AddVec3", i, memcmp(&c1, &c2, sizeof(c1)), 0);

        memset(&c1, 0, sizeof(c1)); memset(&c2, 0, sizeof(c2));
        GV_SubVec3(&a, &b, &c1);
        rc_exe_fn_GV_SubVec3(P(&a), P(&b), P(&c2), 0);
        check("GV_SubVec3", i, memcmp(&c1, &c2, sizeof(c1)), 0);

        a.vx >>= 3; a.vy >>= 3; a.vz >>= 3;       /* keep the squares in range */
        b.vx >>= 3; b.vy >>= 3; b.vz >>= 3;
        check("GV_VecLen3", i, GV_VecLen3(&a), (int)rc_exe_fn_GV_VecLen3(P(&a), 0, 0, 0));
        check("GV_DiffVec3", i, GV_DiffVec3(&a, &b), (int)rc_exe_fn_GV_DiffVec3(P(&a), P(&b), 0, 0));
        check("GV_VecDir2", i, GV_VecDir2(&a), (int)rc_exe_fn_GV_VecDir2(P(&a), 0, 0, 0));
        check("GV_DiffDirU", i, GV_DiffDirU(x, y), (int)rc_exe_fn_GV_DiffDirU(x, y, 0, 0));
        check("GV_DiffDirS", i, GV_DiffDirS(x, y), (int)rc_exe_fn_GV_DiffDirS(x, y, 0, 0));
        check("GV_DiffDirAbs", i, GV_DiffDirAbs(x, y), (int)rc_exe_fn_GV_DiffDirAbs(x, y, 0, 0));
        check("GV_NearExp2", i, GV_NearExp2(x, y), (int)rc_exe_fn_GV_NearExp2(x, y, 0, 0));
        check("GV_NearExp4", i, GV_NearExp4(x, y), (int)rc_exe_fn_GV_NearExp4(x, y, 0, 0));
    }
    /* every recompiled overlay, loaded from the PS1 build, must install into
     * the original memory layout (thunks in executable RAM, relocations) */
    if (psx_mem_init() != 0)
        return 1;
    {
        const RecompOverlay *rc;
        for (rc = mgs_recomp_overlays; rc->name; rc++) {
            char path[1024];
            FILE *f;
            snprintf(path, sizeof(path), "%s/%s.bin", MGS_OBJ_DIR, rc->name);
            f = fopen(path, "rb");
            if (!f || fread((void *)(uintptr_t)rc->ov->base, 1, rc->ov->size, f) != rc->ov->size) {
                printf("FAIL cannot load %s\n", path);
                failures++;
            } else {
                psx_recomp_install(rc->ov);
            }
            if (f)
                fclose(f);
        }
    }
    /* calls through a thunk at an overlay address reach the native code */
    {
        static const RcEntry e = { PSX_OVERLAY_BASE + 0x100, (RcFn)rc_exe_fn_GV_StrCode };
        static const RcOverlay ov = { "thunk", PSX_OVERLAY_BASE, 0x200, PSX_OVERLAY_BASE,
                                      PSX_OVERLAY_BASE + 0x200, &e, 1, 0, 0 };
        int (*fn)(const char *) = (int (*)(const char *))(uintptr_t)e.addr;
        psx_recomp_install(&ov);
        check("thunk", 0, GV_StrCode("snake"), fn("snake"));
    }
    printf(failures ? "recomp: %d failures\n" : "recomp: all checks passed\n", failures);
    return failures != 0;
}
