/*
 * Operating-system specifics, kept apart from the PsyQ-style code because
 * windows.h clashes with PsyQ names (RECT, EnterCriticalSection, ...).
 */
#ifndef PSX_OS_H
#define PSX_OS_H

#include <stddef.h>
#include <stdint.h>

/* map size bytes of zeroed read/write memory exactly at addr */
void *os_map_fixed(uint32_t addr, uint32_t size);

typedef struct os_fiber os_fiber;

/* turn the calling thread into the first fiber */
os_fiber *os_fiber_init_main(void);
/* create a fiber that runs entry() on a stack of stack_size bytes */
os_fiber *os_fiber_create(void (*entry)(void), size_t stack_size);
void      os_fiber_destroy(os_fiber *f);
void      os_fiber_switch(os_fiber *from, os_fiber *to);

#endif
