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

#endif /* MGS_NATIVE_H */
