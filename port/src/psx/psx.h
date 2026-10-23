/*
 * Internal interface of the PlayStation runtime replacement ("HLE layer").
 *
 * The game code is compiled against the original PsyQ headers; this layer is
 * compiled against the host headers and re-declares the few PsyQ structures
 * it needs with identical layouts.
 */
#ifndef PSX_H
#define PSX_H

#include <stdint.h>
#include <stddef.h>

/* ---- PsyQ types (layouts must match psyq_4.4/include) ------------------- */

typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;

typedef struct { short x, y, w, h; } RECT;

typedef struct { u_long tag; u_long code[15]; } DR_ENV;

typedef struct {
    RECT    clip;
    short   ofs[2];
    RECT    tw;
    u_short tpage;
    u_char  dtd;
    u_char  dfe;
    u_char  isbg;
    u_char  r0, g0, b0;
    DR_ENV  dr_env;
} DRAWENV;

typedef struct {
    RECT   disp;
    RECT   screen;
    u_char isinter;
    u_char isrgb24;
    u_char pad0, pad1;
} DISPENV;

typedef struct { short m[3][3]; long t[3]; } MATRIX;
typedef struct { long vx, vy, vz, pad; } VECTOR;
typedef struct { short vx, vy, vz, pad; } SVECTOR;
typedef struct { u_char r, g, b, cd; } CVECTOR;

/* ---- memory map ---------------------------------------------------------- */

/*
 * Like on the console, the game keeps fixed-address heaps in main RAM
 * (0x80000000-0x801FFFFF) and uses the scratchpad at 0x1F800000. Both are
 * mapped at their original addresses. The executable itself is linked below
 * 16 MiB so that the 24-bit pointers in GPU ordering tables stay decodable.
 */
#define PSX_RAM_BASE   0x80000000u
#define PSX_RAM_SIZE   0x00200000u
#define PSX_SPAD_BASE  0x1F800000u
#define PSX_SPAD_SIZE  0x00010000u   /* 1 KiB used, one allocation granule */

/* overlay load address returned by mts_get_bss_tail() (_bss_orgend): the
 * original address, so recompiled overlays find their data where it was */
#define PSX_OVERLAY_BASE 0x800C3208u
/* below it the original executable lived (from 0x80010000); unused natively
 * and blocked so that untranslated accesses from recompiled code fault
 * immediately. The kernel area below stays accessible like on the console. */
#define PSX_MAIN_GUARD_START 0x80010000u
#define PSX_MAIN_GUARD_END   0x800C3000u

int  psx_mem_init(void);

/* GPU ordering-table links hold the low 24 bits of an address */
static inline void *psx_decode_addr24(uint32_t a24)
{
    a24 &= 0xFFFFFF;
    if (a24 < PSX_RAM_SIZE)
        return (void *)(uintptr_t)(PSX_RAM_BASE | a24);
    return (void *)(uintptr_t)a24;
}

/* ---- logging ------------------------------------------------------------- */

void psx_log(const char *fmt, ...);
void psx_fatal(const char *fmt, ...);
/* log a call to a not yet implemented PsyQ function once */
#define PSX_TODO() psx_todo(__func__)
void psx_todo(const char *func);

/* ---- kernel: threads, interrupts ----------------------------------------- */

void psx_kernel_init(void);
/* deliver pending interrupts (vblank, CD, ...) if allowed; cheap to call */
void psx_poll(void);
/* block until the next vblank, then deliver interrupts */
void psx_idle(void);
int  psx_in_critical(void);
uint32_t psx_vblank_count(void);

/* ---- subsystems ---------------------------------------------------------- */

void psx_gpu_init(void);
int  psx_gpu_set_scale(int scale);   /* 1, 2 or 4; returns the scale in use */
extern int psx_gte_widescreen;      /* squeeze 3D projection for 16:9 */
extern int psx_ws_hud;              /* squeeze 2D elements too (ws.c) */
void psx_ws_init(int widescreen, int fit_hud);
void psx_interp_init(int enable);   /* --60fps (interp.c) */
void psx_ws_mark(int x, int y);
int  psx_ws_is_projected(int x, int y);
void psx_ws_new_frame(void);
void psx_ws_movie_frame(void);
int  psx_ws_movie_active(void);
void psx_gpu_vblank(void);          /* present the display area */
void psx_cd_init(const char *image);
void psx_cd_interrupt(void);        /* advance pending CD reads */
void psx_pad_vblank(void);
void psx_spu_init(void);
void psx_spu_vblank(void);          /* mix one frame of audio */
void psx_spu_interrupt(void);
void psx_deliver_event(unsigned long desc, long spec);
void psx_draw_sync_interrupt(void);

/* game-side hook, called once per vblank (src/game/native_options.c) */
void mgs_native_frame(void);

/* ---- platform (SDL) ------------------------------------------------------ */

int      plat_init(int scale, int fullscreen, int render_scale, int widescreen);
void     plat_shutdown(void);
int      plat_pump_events(void);    /* returns 0 when the user quits */
void     plat_present(const uint16_t *vram, int stride, int shift, int x, int y, int w, int h,
                      int rgb24, int enabled, int pillarbox);
uint16_t plat_pad_buttons(int port); /* PS1 button bits, active high */
uint64_t plat_ticks_us(void);
void     plat_sleep_us(uint64_t us);
void     plat_audio_queue(const int16_t *samples, int frames);
int      plat_audio_queued_frames(void);

#endif /* PSX_H */
