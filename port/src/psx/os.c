#include "os.h"

#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

struct os_fiber {
    LPVOID handle;
    void (*entry)(void);
};

void *os_map_fixed(uint32_t addr, uint32_t size, int exec)
{
    void *p = VirtualAlloc((void *)(uintptr_t)addr, size, MEM_RESERVE | MEM_COMMIT,
                           exec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
    return p == (void *)(uintptr_t)addr ? p : NULL;
}

int os_protect_none(uint32_t addr, uint32_t size)
{
    DWORD old;
    return VirtualProtect((void *)(uintptr_t)addr, size, PAGE_NOACCESS, &old) ? 0 : -1;
}

static void CALLBACK fiber_start(LPVOID arg)
{
    ((os_fiber *)arg)->entry();
}

os_fiber *os_fiber_init_main(void)
{
    os_fiber *f = calloc(1, sizeof(*f));
    f->handle = ConvertThreadToFiber(NULL);
    return f->handle ? f : NULL;
}

os_fiber *os_fiber_create(void (*entry)(void), size_t stack_size)
{
    os_fiber *f = calloc(1, sizeof(*f));
    f->entry = entry;
    f->handle = CreateFiber(stack_size, fiber_start, f);
    if (!f->handle) {
        free(f);
        return NULL;
    }
    return f;
}

void os_fiber_destroy(os_fiber *f)
{
    if (f) {
        DeleteFiber(f->handle);
        free(f);
    }
}

void os_fiber_switch(os_fiber *from, os_fiber *to)
{
    (void)from;
    SwitchToFiber(to->handle);
}

#else
#include <sys/mman.h>
#include <ucontext.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

struct os_fiber {
    ucontext_t ctx;
    void      *stack;
};

void *os_map_fixed(uint32_t addr, uint32_t size, int exec)
{
    void *p = mmap((void *)(uintptr_t)addr, size, PROT_READ | PROT_WRITE | (exec ? PROT_EXEC : 0),
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || p != (void *)(uintptr_t)addr)
        return NULL;
    return p;
}

int os_protect_none(uint32_t addr, uint32_t size)
{
    return mprotect((void *)(uintptr_t)addr, size, PROT_NONE);
}

os_fiber *os_fiber_init_main(void)
{
    return calloc(1, sizeof(os_fiber));
}

os_fiber *os_fiber_create(void (*entry)(void), size_t stack_size)
{
    os_fiber *f = calloc(1, sizeof(*f));
    f->stack = malloc(stack_size);
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = stack_size;
    f->ctx.uc_link = NULL;
    makecontext(&f->ctx, entry, 0);
    return f;
}

void os_fiber_destroy(os_fiber *f)
{
    if (f) {
        free(f->stack);
        free(f);
    }
}

void os_fiber_switch(os_fiber *from, os_fiber *to)
{
    swapcontext(&from->ctx, &to->ctx);
}
#endif
