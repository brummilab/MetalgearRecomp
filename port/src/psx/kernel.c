/*
 * PlayStation kernel replacement: threads, critical sections, interrupts,
 * events and assorted BIOS calls.
 *
 * Threads are host fibers. Interrupts cannot preempt native code, so they are
 * delivered at safe points instead: whenever the game waits (VSync, the idle
 * task) or calls into this layer. A handler may retarget the "current TCB"
 * in the kernel tables, exactly like the MTS scheduler does on the console;
 * after the handler returns we switch to that thread, which is what the
 * console's return-from-exception would do.
 */
#include "psx.h"

#include "os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- kernel tables (struct ToT / TCBH / TCB from kernel.h) -------------- */

#define NREGS 40

struct TCB {
    long          status;
    long          mode;
    unsigned long reg[NREGS];
    long          system[6];
};
struct TCBH { struct TCB *entry; long flag; };
struct ToT  { unsigned long *head; long size; };

#define MAX_THREADS 16
#define THREAD_STACK_SIZE (256 * 1024)

/* The game reads these through (struct ToT *)0x100 on the console; the
 * native build points it here instead (see tools/asm2c.py FIXUPS). */
struct ToT psx_ToT[32];

static struct TCBH tcbh;
static struct TCB  tcbs[MAX_THREADS];

typedef struct {
    int   used;
    int   crit;             /* per-thread interrupt state (SR.IEc) */
    long (*entry)(void);
    os_fiber *fiber;
} Thread;

static Thread threads[MAX_THREADS];
static int    current;       /* index of the running thread */
static int    crit = 1;      /* interrupts disabled during boot */
static int    in_irq;

/* ---- interrupts ---------------------------------------------------------- */

#define VBLANK_US (1000000.0 / 59.94)

static void (*vsync_callback)(void);
static uint32_t vblank_hw;          /* counted by "hardware" */
static uint32_t vblank_delivered;   /* handlers run for these */
static double   next_vblank_us;
static int      quit_requested;

uint32_t psx_vblank_count(void) { return vblank_hw; }
int psx_in_critical(void) { return crit || in_irq; }

static void switch_to(int target);

static void hw_tick(void)
{
    double now = (double)plat_ticks_us();
    if (next_vblank_us == 0)
        next_vblank_us = now + VBLANK_US;
    if (now < next_vblank_us)
        return;
    vblank_hw++;
    next_vblank_us += VBLANK_US;
    if (now - next_vblank_us > 4 * VBLANK_US)
        next_vblank_us = now + VBLANK_US;   /* we fell behind, don't spiral */

    if (!plat_pump_events())
        quit_requested = 1;
    psx_gpu_vblank();
    psx_pad_vblank();
    psx_spu_vblank();
}

void psx_poll(void)
{
    int target;

    hw_tick();
    if (quit_requested) {
        psx_log("quit requested\n");
        exit(0);
    }
    if (crit || in_irq)
        return;

    in_irq = 1;
    while (vblank_delivered != vblank_hw) {
        vblank_delivered++;
        if (vsync_callback)
            vsync_callback();
    }
    psx_cd_interrupt();
    psx_spu_interrupt();
    psx_draw_sync_interrupt();
    in_irq = 0;

    /* return-from-exception into whatever thread the handlers selected */
    target = (int)(tcbh.entry - tcbs);
    if (target != current && target >= 0 && target < MAX_THREADS && threads[target].used)
        switch_to(target);
}

void psx_idle(void)
{
    double now = (double)plat_ticks_us();
    if (next_vblank_us > now)
        plat_sleep_us((uint64_t)(next_vblank_us - now));
    psx_poll();
}

int VSyncCallback(void (*f)(void))
{
    vsync_callback = f;
    return 0;
}

int VSync(int mode)
{
    static uint32_t last;
    uint32_t start = vblank_hw;

    if (mode < 0) {
        /* games busy-wait on this counter; let hardware time pass */
        if (!in_irq)
            hw_tick();
        return (int)vblank_hw;
    }
    if (mode == 1) {
        psx_poll();
        return 0;
    }
    if (mode == 0) {
        while (vblank_hw == start)
            psx_idle();
    } else {
        while (vblank_hw - last < (uint32_t)mode)
            psx_idle();
    }
    last = vblank_hw;
    return 0;
}

/* ---- critical sections --------------------------------------------------- */

int EnterCriticalSection(void)
{
    int was = !crit;
    crit = 1;
    return was;
}

void ExitCriticalSection(void)
{
    crit = 0;
    psx_poll();
}

void SwEnterCriticalSection(void) { crit = 1; }

void SwExitCriticalSection(void)
{
    crit = 0;
    psx_poll();
}

/* ---- threads ------------------------------------------------------------- */

static void thread_main(void)
{
    threads[current].entry();
    psx_fatal("thread %d returned from its entry point", current);
}

static void switch_to(int target)
{
    int from = current;

    if (target == from)
        return;
    threads[from].crit = crit;
    current = target;
    crit = threads[target].crit;
    tcbh.entry = &tcbs[target];
    os_fiber_switch(threads[from].fiber, threads[target].fiber);
}

long SetConf(unsigned long ev, unsigned long tcb, unsigned long sp)
{
    (void)ev; (void)sp;
    if (tcb > MAX_THREADS)
        psx_fatal("SetConf: %lu threads requested, %d supported", tcb, MAX_THREADS);
    return 1;
}

long OpenTh(long (*func)(void), unsigned long sp, unsigned long gp)
{
    int i;
    Thread *t;

    (void)sp; (void)gp; /* the game's PS1-sized stacks are far too small */
    /* never reuse the running thread's slot: its fiber is still executing */
    for (i = 1; i < MAX_THREADS && (threads[i].used || i == current); i++)
        ;
    if (i == MAX_THREADS)
        return -1;
    t = &threads[i];
    t->used = 1;
    t->crit = 1;
    t->entry = func;
    memset(&tcbs[i], 0, sizeof(tcbs[i]));
    tcbs[i].status = 0x4000; /* TcbStACTIVE */
    /* a previous fiber of this slot has finished (CloseTh) and is not running */
    os_fiber_destroy(t->fiber);
    t->fiber = os_fiber_create(thread_main, THREAD_STACK_SIZE);
    if (!t->fiber)
        psx_fatal("cannot create thread");
    return (long)(0xFF000000u | (unsigned)i);
}

int ChangeTh(long tid)
{
    int i = (int)(tid & 0xFFFF);
    if (i < 0 || i >= MAX_THREADS || !threads[i].used)
        psx_fatal("ChangeTh: invalid thread 0x%lx", (unsigned long)tid);
    switch_to(i);
    return 1;
}

int CloseTh(long tid)
{
    int i = (int)(tid & 0xFFFF);
    if (i <= 0 || i >= MAX_THREADS)
        return 0;
    /* the fiber/stack is recycled by the next OpenTh of this slot */
    threads[i].used = 0;
    tcbs[i].status = 0x1000; /* TcbStUNUSED */
    return 1;
}

unsigned long GetGp(void) { return 0; }

unsigned long GetSp(void)
{
    volatile int marker;
    return (unsigned long)(uintptr_t)&marker;
}

void psx_kernel_init(void)
{
    /* thread 0 is the boot context (the host main thread) */
    threads[0].used = 1;
    threads[0].crit = 1;
    current = 0;
    tcbh.entry = &tcbs[0];
    threads[0].fiber = os_fiber_init_main();
    if (!threads[0].fiber)
        psx_fatal("cannot convert the main thread into a fiber");
    psx_ToT[1].head = (unsigned long *)&tcbh;
    psx_ToT[1].size = sizeof(tcbh);
    psx_ToT[2].head = (unsigned long *)tcbs;
    psx_ToT[2].size = sizeof(tcbs);
}

/* ---- events ---------------------------------------------------------------- */

#define MAX_EVENTS 32

typedef struct {
    int           used;
    int           enabled;
    int           fired;
    unsigned long desc;
    long          spec;
    long          mode;
    long        (*func)(void);
} Event;

static Event events[MAX_EVENTS];

long OpenEvent(unsigned long desc, long spec, long mode, long (*func)(void))
{
    int i;
    for (i = 0; i < MAX_EVENTS; i++) {
        if (!events[i].used) {
            memset(&events[i], 0, sizeof(events[i]));
            events[i].used = 1;
            events[i].desc = desc;
            events[i].spec = spec;
            events[i].mode = mode;
            events[i].func = func;
            return (long)(0xF1000000u | (unsigned)i);
        }
    }
    return -1;
}

static Event *event_get(long id)
{
    int i = (int)(id & 0xFFFF);
    if ((id & 0xFF000000) != 0xF1000000 || i >= MAX_EVENTS || !events[i].used)
        return NULL;
    return &events[i];
}

long CloseEvent(long id)
{
    Event *e = event_get(id);
    if (!e)
        return 0;
    e->used = 0;
    return 1;
}

long EnableEvent(long id)
{
    Event *e = event_get(id);
    if (!e)
        return 0;
    e->enabled = 1;
    return 1;
}

long DisableEvent(long id)
{
    Event *e = event_get(id);
    if (!e)
        return 0;
    e->enabled = 0;
    return 1;
}

long TestEvent(long id)
{
    Event *e = event_get(id);
    psx_poll();
    if (!e || !e->fired)
        return 0;
    e->fired = 0;
    return 1;
}

long WaitEvent(long id)
{
    while (!TestEvent(id))
        psx_idle();
    return 1;
}

/* raise an event for (desc, spec): used by the memory card emulation */
void psx_deliver_event(unsigned long desc, long spec)
{
    int i;
    for (i = 0; i < MAX_EVENTS; i++) {
        Event *e = &events[i];
        if (!e->used || !e->enabled || e->desc != desc || e->spec != spec)
            continue;
        if (e->mode == 0x1000 /* EvMdINTR */ && e->func)
            e->func();
        else
            e->fired = 1;
    }
}

/* ---- misc BIOS ------------------------------------------------------------- */

int ResetCallback(void) { vsync_callback = NULL; return 0; }
int StopCallback(void)  { vsync_callback = NULL; return 0; }
void SetMem(long n)     { (void)n; }
void _96_init(void)     {}
void _96_remove(void)   {}

long GetRCnt(unsigned long spec)
{
    /* root counters: only used for load-time statistics */
    (void)spec;
    return (long)((plat_ticks_us() * 15734 / 1000000) & 0xFFFF); /* hblanks */
}

long LoadExec(char *file, unsigned long sp, unsigned long size)
{
    (void)sp; (void)size;
    psx_log("LoadExec(%s): the game asked to restart, exiting\n", file ? file : "?");
    exit(0);
    return 0;
}

/* PsyQ libc rand(): 15-bit LCG, the game relies on the range */
static unsigned long rand_next = 0x24040001;

int psx_rand(void)
{
    rand_next = rand_next * 1103515245 + 12345;
    return (int)((rand_next >> 16) & 0x7FFF);
}

void psx_srand(unsigned int seed) { rand_next = seed; }

/* PC file server (debug builds only) */
long PClseek(long fd, long offset, long mode) { (void)fd; (void)offset; (void)mode; return -1; }

/* ---- logging --------------------------------------------------------------- */

#include <stdarg.h>

void psx_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void psx_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

void psx_todo(const char *func)
{
    static const char *seen[256];
    static int n;
    int i;
    for (i = 0; i < n; i++)
        if (seen[i] == func)
            return;
    if (n < 256)
        seen[n++] = func;
    psx_log("TODO: %s not implemented\n", func);
}

int PCcreat(char *name, int perms) { (void)name; (void)perms; return -1; }
int PCwrite(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return -1; }
