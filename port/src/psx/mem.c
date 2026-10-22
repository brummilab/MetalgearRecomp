/*
 * Main RAM and scratchpad at their original PlayStation addresses.
 */
#include "psx.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>

static void *map_fixed(uint32_t addr, uint32_t size)
{
    return VirtualAlloc((void *)(uintptr_t)addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
#else
#include <sys/mman.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static void *map_fixed(uint32_t addr, uint32_t size)
{
    void *p = mmap((void *)(uintptr_t)addr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || p != (void *)(uintptr_t)addr)
        return NULL;
    return p;
}
#endif

/* _bss_orgend (end of the original .bss = overlay base) is defined by the
 * linker: --defsym=_bss_orgend=PSX_OVERLAY_BASE, see CMakeLists.txt */

int psx_mem_init(void)
{
    if (!map_fixed(PSX_RAM_BASE, PSX_RAM_SIZE)) {
        psx_fatal("cannot map PS1 RAM at 0x%08X (needs a 32-bit build; on Windows a\n"
                  "64-bit OS and a large-address-aware executable)", PSX_RAM_BASE);
        return -1;
    }
    if (!map_fixed(PSX_SPAD_BASE, PSX_SPAD_SIZE)) {
        psx_fatal("cannot map scratchpad at 0x%08X", PSX_SPAD_BASE);
        return -1;
    }
    memset((void *)(uintptr_t)PSX_RAM_BASE, 0, PSX_RAM_SIZE);
    memset((void *)(uintptr_t)PSX_SPAD_BASE, 0, PSX_SPAD_SIZE);
    return 0;
}
