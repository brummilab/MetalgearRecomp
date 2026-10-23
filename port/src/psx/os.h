/*
 * Operating-system specifics, kept apart from the PsyQ-style code because
 * windows.h clashes with PsyQ names (RECT, EnterCriticalSection, ...).
 */
#ifndef PSX_OS_H
#define PSX_OS_H

#include <stddef.h>
#include <stdint.h>

/* map size bytes of zeroed read/write memory exactly at addr; exec also
 * allows running code there (thunks of recompiled overlays) */
void *os_map_fixed(uint32_t addr, uint32_t size, int exec);
/* make [addr, addr + size) inaccessible (page aligned) */
int   os_protect_none(uint32_t addr, uint32_t size);

/* end of the program's loaded data (address after its last data section) */
uint32_t os_image_data_end(void);

/* on a crash, log the exception, the faulting address and probable return
 * addresses (to map with addr2line/nm on the unstripped build) */
void  os_install_crash_handler(void);

typedef struct os_fiber os_fiber;

/* turn the calling thread into the first fiber */
os_fiber *os_fiber_init_main(void);
/* create a fiber that runs entry() on a stack of stack_size bytes */
os_fiber *os_fiber_create(void (*entry)(void), size_t stack_size);
void      os_fiber_destroy(os_fiber *f);
void      os_fiber_switch(os_fiber *from, os_fiber *to);

#endif
