/*
 * Runtime support for statically recompiled overlays (tools/recomp.py).
 *
 * After the game has loaded an overlay image from the disc to its original
 * address, psx_recomp_install() makes it usable from native code:
 *  - at every recompiled entry point in the overlay's code area a small x86
 *    jump to the native function is written, so pointers to overlay
 *    functions (chara tables, actor callbacks) can be called directly;
 *  - data words that point at main executable symbols are relocated to the
 *    native symbols.
 */
#include "psx.h"
#include "recomp.h"

#include <string.h>

void psx_recomp_install(const RcOverlay *ov)
{
    int i;

    for (i = 0; i < ov->n_entries; i++) {
        uint8_t *p = (uint8_t *)(uintptr_t)ov->entries[i].addr;
        uint32_t target = (uint32_t)(uintptr_t)ov->entries[i].fn;
        p[0] = 0xB8;                    /* mov eax, imm32 */
        memcpy(p + 1, &target, 4);
        p[5] = 0xFF;                    /* jmp eax */
        p[6] = 0xE0;
    }
    for (i = 0; i < ov->n_relocs; i++)
        *(uint32_t *)(uintptr_t)ov->relocs[i].addr = (uint32_t)(uintptr_t)ov->relocs[i].value;
    psx_log("recomp: overlay %s installed (%d entry points, %d relocations)\n",
            ov->name, ov->n_entries, ov->n_relocs);
}

uint32_t rc_unresolved(uint32_t addr, const char *what)
{
    psx_log("recomp: call to unresolved address 0x%08X (%s)\n", addr, what);
    return 0;
}

void rc_unknown(uint32_t pc, uint32_t insn)
{
    psx_log("recomp: unknown instruction %08X at %08X\n", insn, pc);
}

void rc_syscall(uint32_t pc)
{
    psx_log("recomp: syscall at %08X ignored\n", pc);
}
