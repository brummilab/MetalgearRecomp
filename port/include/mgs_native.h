/*
 * Force-included into every game translation unit of the native build.
 *
 * The GTE inline asm of the PS1 sources is rewritten by tools/asm2c.py into
 * the GTE_* macros below, which call the GTE emulator in src/psx/gte.c.
 */
#ifndef MGS_NATIVE_H
#define MGS_NATIVE_H

#define MGS_NATIVE 1

unsigned int psx_gte_read_data(int reg);
void         psx_gte_write_data(int reg, unsigned int value);
unsigned int psx_gte_read_ctrl(int reg);
void         psx_gte_write_ctrl(int reg, unsigned int value);
void         psx_gte_command(unsigned int cmd);

#define GTE_RD(n)    psx_gte_read_data(n)
#define GTE_WD(n, v) psx_gte_write_data((n), (unsigned int)(v))
#define GTE_RC(n)    psx_gte_read_ctrl(n)
#define GTE_WC(n, v) psx_gte_write_ctrl((n), (unsigned int)(v))
#define GTE_OP(c)    psx_gte_command(c)

/* 60 fps interpolation: the rotation matrix was loaded from this address */
void psx_gte_note_rt(unsigned int addr);
#define GTE_NOTE_RT(a) psx_gte_note_rt(a)

/* runtime hooks used by the source fixups in tools/asm2c.py */
extern char psx_ToT[];
void psx_idle(void);
void mgs_native_overlay_loaded(int id);
extern int mgs_stage_bin_id;        /* streamed stage binary being loaded, -1: none */
void mgs_native_stage_loaded(void);
extern int psx_watch_stack;         /* --watch-stack: debug return-address smashing */
void psx_watch_return_slot(void *slot);
int  mgs_native_overlay_available(const char *name);

/* PS1 BIOS file I/O (memory card) and PsyQ's 15-bit rand() */
#define open   psx_open
#define read   psx_read
#define write  psx_write
#define close  psx_close
#define lseek  psx_lseek
#define rand   psx_rand
#define srand  psx_srand
#define fprintf mgs_fprintf

#endif /* MGS_NATIVE_H */
