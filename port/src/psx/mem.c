/*
 * Main RAM and scratchpad at their original PlayStation addresses.
 */
#include "psx.h"
#include "os.h"

#include <string.h>

/* _bss_orgend (end of the original .bss = overlay base) is defined by the
 * linker: --defsym=_bss_orgend=PSX_OVERLAY_BASE, see CMakeLists.txt */

int psx_mem_guard;

int psx_mem_init(void)
{
    /* the GPU links primitives with 24-bit addresses: game data must lie
     * below 16 MiB (see psx_decode_addr24) */
    if (os_image_data_end() > 0x01000000u) {
        psx_fatal("program data ends at 0x%08X, above the 16 MiB the PlayStation GPU\n"
                  "can address (build problem: too much static data)", os_image_data_end());
        return -1;
    }
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
    /* original executable: unused natively. For debugging, any stray access
     * (e.g. an untranslated address in recompiled code) can be made to fault;
     * by default it reads zeros, as harmless as on the console */
    if (psx_mem_guard && os_protect_none(PSX_MAIN_GUARD_START, PSX_MAIN_GUARD_END - PSX_MAIN_GUARD_START) != 0)
        psx_log("mem: cannot protect the original executable area\n");
    return 0;
}
