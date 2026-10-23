/*
 * Main RAM and scratchpad at their original PlayStation addresses.
 */
#include "psx.h"
#include "os.h"

#include <string.h>

/* _bss_orgend (end of the original .bss = overlay base) is defined by the
 * linker: --defsym=_bss_orgend=PSX_OVERLAY_BASE, see CMakeLists.txt */

int psx_mem_init(void)
{
    /* executable: recompiled overlays get x86 entry thunks in their code area */
    if (!os_map_fixed(PSX_RAM_BASE, PSX_RAM_SIZE, 1)) {
        psx_fatal("cannot map PS1 RAM at 0x%08X (needs a 32-bit build; on Windows a\n"
                  "64-bit OS and a large-address-aware executable)", PSX_RAM_BASE);
        return -1;
    }
    if (!os_map_fixed(PSX_SPAD_BASE, PSX_SPAD_SIZE, 0)) {
        psx_fatal("cannot map scratchpad at 0x%08X", PSX_SPAD_BASE);
        return -1;
    }
    memset((void *)(uintptr_t)PSX_RAM_BASE, 0, PSX_RAM_SIZE);
    memset((void *)(uintptr_t)PSX_SPAD_BASE, 0, PSX_SPAD_SIZE);
    /* original executable: unused natively, so any stray access (e.g. an
     * untranslated address in recompiled code) faults */
    if (os_protect_none(PSX_MAIN_GUARD_START, PSX_MAIN_GUARD_END - PSX_MAIN_GUARD_START) != 0)
        psx_log("mem: cannot protect the original executable area\n");
    return 0;
}
