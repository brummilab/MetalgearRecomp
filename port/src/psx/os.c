#ifndef _WIN32
#define _GNU_SOURCE     /* REG_EIP */
#endif
#include "os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char __executable_start[] __attribute__((weak));
extern char etext[] __attribute__((weak));
extern char _end[] __attribute__((weak));
extern int mts_active_task_800C0DB0 __attribute__((weak));   /* game's current task */

/* registers, raw stack words and the game task, for crash reports */
static void crash_context(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx, uint32_t esi,
                          uint32_t edi, uint32_t ebp, const uint32_t *sp, int words)
{
    int i;
    fprintf(stderr, "CRASH: eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X\n",
            (unsigned)eax, (unsigned)ebx, (unsigned)ecx, (unsigned)edx, (unsigned)esi, (unsigned)edi,
            (unsigned)ebp, (unsigned)(uintptr_t)sp);
    if (&mts_active_task_800C0DB0)
        fprintf(stderr, "CRASH: game task %d\n", mts_active_task_800C0DB0);
    fprintf(stderr, "CRASH: stack:");
    for (i = 0; i < words && i < 16; i++)
        fprintf(stderr, " %08X", (unsigned)sp[i]);
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* return addresses on the stack: words pointing into the program's code */
static void crash_backtrace(const uint32_t *sp, int words, uint32_t lo, uint32_t hi)
{
    int i, n = 0;
    fprintf(stderr, "CRASH: probable call chain:");
    for (i = 0; i < words && n < 24; i++) {
        uint32_t v = sp[i];
        if (v >= lo && v < hi) {
            fprintf(stderr, " %08X", v);
            n++;
        }
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

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

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *e)
{
    static volatile LONG reported;
    const EXCEPTION_RECORD *r = e->ExceptionRecord;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)dos + dos->e_lfanew);
    uint32_t base = (uint32_t)(uintptr_t)dos;

    switch (r->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_PRIV_INSTRUCTION: case EXCEPTION_IN_PAGE_ERROR:
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;   /* C++/debug/benign exceptions */
    }
    if (InterlockedExchange(&reported, 1))
        return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr, "\nCRASH: exception 0x%08lX at EIP %08lX", (unsigned long)r->ExceptionCode,
            (unsigned long)e->ContextRecord->Eip);
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        fprintf(stderr, " (%s address %08lX)", r->ExceptionInformation[0] ? "write to" : "read from",
                (unsigned long)r->ExceptionInformation[1]);
    fprintf(stderr, "\n");
    fflush(stderr);
    {
        /* only as far as the stack's memory region goes (fiber stacks are small) */
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t sp = (uintptr_t)e->ContextRecord->Esp, end;
        int words = 0;
        if (VirtualQuery((void *)sp, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT) {
            end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            words = (int)((end - sp) / 4);
            if (words > 1024)
                words = 1024;
        }
        const CONTEXT *c = e->ContextRecord;
        crash_context(c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, (const uint32_t *)sp, words);
        crash_backtrace((const uint32_t *)sp, words, base + nt->OptionalHeader.BaseOfCode,
                        base + nt->OptionalHeader.BaseOfCode + nt->OptionalHeader.SizeOfCode);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

uint32_t os_image_data_end(void)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)dos + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    uint32_t end = 0;
    int i;

    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint32_t e = (uint32_t)(uintptr_t)dos + sec[i].VirtualAddress + sec[i].Misc.VirtualSize;
        /* debug info and relocations are discardable (long section names
         * like .debug_info appear as "/NN", so go by the flag) */
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_DISCARDABLE) && e > end)
            end = e;
    }
    return end;
}

void os_install_crash_handler(void)
{
    /* vectored: sees the fault first, also on fiber stacks and in threads
     * whose own handlers would end the process silently */
    AddVectoredExceptionHandler(1, crash_filter);
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
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

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

uint32_t os_image_data_end(void)
{
    return (uint32_t)(uintptr_t)_end;
}

static void crash_signal(int sig, siginfo_t *info, void *uc_)
{
    ucontext_t *uc = uc_;
#if defined(__i386__) && defined(REG_EIP)
    fprintf(stderr, "\nCRASH: signal %d at EIP %08X (address %08X)\n", sig,
            (unsigned)uc->uc_mcontext.gregs[REG_EIP], (unsigned)(uintptr_t)info->si_addr);
    fflush(stderr);
    {
        const greg_t *g = uc->uc_mcontext.gregs;
        crash_context(g[REG_EAX], g[REG_EBX], g[REG_ECX], g[REG_EDX], g[REG_ESI], g[REG_EDI], g[REG_EBP],
                      (const uint32_t *)(uintptr_t)g[REG_ESP], 16);
    }
    crash_backtrace((const uint32_t *)(uintptr_t)uc->uc_mcontext.gregs[REG_ESP], 256,
                    (uint32_t)(uintptr_t)__executable_start, (uint32_t)(uintptr_t)etext);
#else
    (void)uc;
    fprintf(stderr, "\nCRASH: signal %d (address %p)\n", sig, info->si_addr);
#endif
    fflush(stderr);
    _exit(128 + sig);
}

void os_install_crash_handler(void)
{
    static char alt[65536];
    stack_t ss;
    struct sigaction sa;

    ss.ss_sp = alt;                     /* also catches fiber stack overflows */
    ss.ss_size = sizeof(alt);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    sa.sa_sigaction = crash_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
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
