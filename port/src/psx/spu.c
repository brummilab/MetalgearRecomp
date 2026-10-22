/*
 * SPU emulation behind the libspu API: 512 KiB sound RAM, 24 ADPCM voices
 * with hardware-style ADSR envelopes, loop points, the SPU IRQ (used by the
 * game's streaming code) and a mixer feeding the host audio device.
 *
 * CD audio input: XA-ADPCM decoded by xa.c, resampled to 44.1 kHz here.
 * Not emulated yet: reverb, noise, pitch LFO.
 */
#include "psx.h"

#include <math.h>
#include <string.h>

#define SPU_RAM_SIZE   (512 * 1024)
#define NUM_VOICES     24
#define OUTPUT_RATE    44100

typedef struct { short left, right; } SpuVolume;

typedef struct {
    u_long    voice;
    u_long    mask;
    SpuVolume volume;
    SpuVolume volmode;
    SpuVolume volumex;
    u_short   pitch;
    u_short   note;
    u_short   sample_note;
    short     envx;
    u_long    addr;
    u_long    loop_addr;
    long      a_mode;
    long      s_mode;
    long      r_mode;
    u_short   ar, dr, sr, rr, sl;
    u_short   adsr1, adsr2;
} SpuVoiceAttr;

typedef struct { SpuVolume volume; long reverb; long mix; } SpuExtAttr;

typedef struct {
    u_long     mask;
    SpuVolume  mvol;
    SpuVolume  mvolmode;
    SpuVolume  mvolx;
    SpuExtAttr cd;
    SpuExtAttr ext;
} SpuCommonAttr;

typedef struct {
    u_long    mask;
    long      mode;
    SpuVolume depth;
    long      delay;
    long      feedback;
} SpuReverbAttr;

typedef void (*SpuIRQCallbackProc)(void);

/* mask bits (libspu.h) */
#define V_VOLL   (1 << 0)
#define V_VOLR   (1 << 1)
#define V_PITCH  (1 << 4)
#define V_NOTE   (1 << 5)
#define V_SNOTE  (1 << 6)
#define V_WDSA   (1 << 7)
#define V_AMODE  (1 << 8)
#define V_SMODE  (1 << 9)
#define V_RMODE  (1 << 10)
#define V_AR     (1 << 11)
#define V_DR     (1 << 12)
#define V_SR     (1 << 13)
#define V_RR     (1 << 14)
#define V_SL     (1 << 15)
#define V_LSAX   (1 << 16)
#define V_ADSR1  (1 << 17)
#define V_ADSR2  (1 << 18)

#define C_MVOLL  (1 << 0)
#define C_MVOLR  (1 << 1)
#define C_CDVOLL (1 << 6)
#define C_CDVOLR (1 << 7)
#define C_CDMIX  (1 << 9)

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    /* registers */
    int16_t  vol_l, vol_r;
    uint16_t pitch;
    uint32_t start;         /* byte address */
    uint32_t loop;          /* byte address */
    uint16_t adsr1, adsr2;
    uint16_t note, sample_note;

    /* playback */
    int      keyed;         /* key-on state from the CPU's view */
    uint32_t cur;           /* current block address */
    uint32_t counter;       /* 12-bit fractional sample position */
    int16_t  decoded[28];
    int      block_pos;     /* sample index within decoded block */
    int16_t  hist1, hist2;
    int16_t  prev, last;    /* for interpolation */
    int      env_phase;
    int32_t  env_level;
    int32_t  env_counter;
} Voice;

static uint8_t   spu_ram[SPU_RAM_SIZE];
static Voice     voices[NUM_VOICES];
static int16_t   main_vol_l = 0x3FFF, main_vol_r = 0x3FFF;
static uint32_t  transfer_addr;
static uint32_t  irq_addr;
static int       irq_enabled;
static int       irq_pending;
static SpuIRQCallbackProc irq_callback;
static double    sample_accum;

/* CD (XA) audio input, already resampled to OUTPUT_RATE */
#define XA_RING 65536
static int16_t   xa_ring[XA_RING][2];
static unsigned  xa_read, xa_write;
static int16_t   cd_vol_l, cd_vol_r;
static int       cd_mix;
static double    xa_phase;
static int16_t   xa_last[2];

void psx_spu_xa_queue(const int16_t *lr, int frames, int rate)
{
    double step = (double)rate / OUTPUT_RATE;
    int i = 0;
    /* linear interpolation between the previous and the current input frame */
    while (i < frames) {
        double f = xa_phase;
        int16_t l = (int16_t)(xa_last[0] + (lr[i * 2] - xa_last[0]) * f);
        int16_t r = (int16_t)(xa_last[1] + (lr[i * 2 + 1] - xa_last[1]) * f);
        if (xa_write - xa_read < XA_RING) {
            xa_ring[xa_write % XA_RING][0] = l;
            xa_ring[xa_write % XA_RING][1] = r;
            xa_write++;
        }
        xa_phase += step;
        while (xa_phase >= 1.0 && i < frames) {
            xa_phase -= 1.0;
            xa_last[0] = lr[i * 2];
            xa_last[1] = lr[i * 2 + 1];
            i++;
        }
    }
}

void psx_spu_xa_flush(void)
{
    xa_read = xa_write = 0;
    xa_phase = 0;
}

/* ---- ADPCM ------------------------------------------------------------------ */

static const int adpcm_pos[5] = { 0, 60, 115, 98, 122 };
static const int adpcm_neg[5] = { 0, 0, -52, -55, -60 };

static void check_irq(uint32_t addr)
{
    if (irq_enabled && (addr & ~15u) == (irq_addr & ~15u))
        irq_pending = 1;
}

static void decode_block(Voice *v)
{
    const uint8_t *b = &spu_ram[v->cur & (SPU_RAM_SIZE - 1) & ~15u];
    int shift = b[0] & 0x0F, filter = (b[0] >> 4) & 0x07, i;
    if (shift > 12)
        shift = 9;
    if (filter > 4)
        filter = 4;
    check_irq(v->cur);
    for (i = 0; i < 28; i++) {
        int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 0xF;
        int32_t s = (int16_t)(nib << 12) >> shift;
        s += (v->hist1 * adpcm_pos[filter] + v->hist2 * adpcm_neg[filter] + 32) >> 6;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        v->hist2 = v->hist1;
        v->hist1 = (int16_t)s;
        v->decoded[i] = (int16_t)s;
    }
}

/* flags of the current block: bit0 end, bit1 repeat, bit2 loop start */
static uint8_t block_flags(Voice *v)
{
    return spu_ram[(v->cur + 1) & (SPU_RAM_SIZE - 1)];
}

/* ---- ADSR (psx-spx "SPU ADSR") ---------------------------------------------------- */

static void env_params(Voice *v, int *exp, int *dec, int *shift, int *step, int32_t *target)
{
    uint16_t a1 = v->adsr1, a2 = v->adsr2;
    switch (v->env_phase) {
    case ENV_ATTACK:
        *exp = (a1 >> 15) & 1; *dec = 0;
        *shift = (a1 >> 10) & 0x1F; *step = 7 - ((a1 >> 8) & 3);
        *target = 0x7FFF;
        break;
    case ENV_DECAY:
        *exp = 1; *dec = 1;
        *shift = (a1 >> 4) & 0xF; *step = -8;
        *target = (((a1 & 0xF) + 1) * 0x800);
        break;
    case ENV_SUSTAIN:
        *exp = (a2 >> 15) & 1; *dec = (a2 >> 14) & 1;
        *shift = (a2 >> 8) & 0x1F;
        *step = *dec ? -8 + ((a2 >> 6) & 3) : 7 - ((a2 >> 6) & 3);
        *target = *dec ? 0 : 0x7FFF;
        break;
    default: /* release */
        *exp = (a2 >> 5) & 1; *dec = 1;
        *shift = a2 & 0x1F; *step = -8;
        *target = 0;
        break;
    }
}

static void env_tick(Voice *v)
{
    int exp, dec, shift, step, cycles;
    int32_t target, adsr_step;

    if (v->env_phase == ENV_OFF)
        return;
    env_params(v, &exp, &dec, &shift, &step, &target);

    cycles = 1 << (shift > 11 ? shift - 11 : 0);
    adsr_step = step << (shift < 11 ? 11 - shift : 0);
    if (exp && !dec && v->env_level > 0x6000)
        cycles *= 4;
    if (exp && dec)
        adsr_step = (adsr_step * v->env_level) >> 15;

    if (--v->env_counter > 0)
        return;
    v->env_counter = cycles;
    v->env_level += adsr_step;
    if (v->env_level < 0) v->env_level = 0;
    if (v->env_level > 0x7FFF) v->env_level = 0x7FFF;

    switch (v->env_phase) {
    case ENV_ATTACK:
        if (v->env_level >= 0x7FFF)
            v->env_phase = ENV_DECAY;
        break;
    case ENV_DECAY:
        if (v->env_level <= target)
            v->env_phase = ENV_SUSTAIN;
        break;
    case ENV_RELEASE:
        if (v->env_level <= 0)
            v->env_phase = ENV_OFF;
        break;
    default:
        break;
    }
}

/* ---- voice playback ----------------------------------------------------------------- */

static void key_on(Voice *v)
{
    v->keyed = 1;
    v->cur = v->start;
    v->counter = 0;
    v->block_pos = 0;
    v->hist1 = v->hist2 = 0;
    v->prev = v->last = 0;
    v->env_phase = ENV_ATTACK;
    v->env_level = 0;
    v->env_counter = 0;
    decode_block(v);
}

static void key_off(Voice *v)
{
    v->keyed = 0;
    if (v->env_phase != ENV_OFF)
        v->env_phase = ENV_RELEASE;
}

static int32_t voice_sample(Voice *v)
{
    int32_t out;
    uint32_t step;

    if (v->env_phase == ENV_OFF)
        return 0;

    /* linear interpolation between the last two decoded samples */
    out = v->prev + (((v->last - v->prev) * (int32_t)(v->counter & 0xFFF)) >> 12);
    out = (out * v->env_level) >> 15;

    step = v->pitch > 0x4000 ? 0x4000 : v->pitch;
    v->counter += step;
    while (v->counter >= 0x1000) {
        v->counter -= 0x1000;
        v->prev = v->last;
        v->last = v->decoded[v->block_pos];
        if (++v->block_pos == 28) {
            uint8_t flags = block_flags(v);
            v->block_pos = 0;
            if (flags & 4)
                v->loop = v->cur;
            if (flags & 1) {
                v->cur = v->loop;
                if (!(flags & 2)) {
                    v->env_phase = ENV_OFF;
                    v->env_level = 0;
                }
            } else {
                v->cur += 16;
            }
            decode_block(v);
        }
    }
    env_tick(v);
    return out;
}

static void mix(int frames)
{
    int16_t buf[2 * 1024];
    int i, n;

    while (frames > 0) {
        n = frames > 1024 ? 1024 : frames;
        for (i = 0; i < n; i++) {
            int32_t l = 0, r = 0;
            int k;
            for (k = 0; k < NUM_VOICES; k++) {
                Voice *v = &voices[k];
                int32_t s;
                if (v->env_phase == ENV_OFF)
                    continue;
                s = voice_sample(v);
                l += (s * v->vol_l) >> 15;
                r += (s * v->vol_r) >> 15;
            }
            if (xa_read != xa_write) {
                if (cd_mix) {
                    l += (xa_ring[xa_read % XA_RING][0] * cd_vol_l) >> 15;
                    r += (xa_ring[xa_read % XA_RING][1] * cd_vol_r) >> 15;
                }
                xa_read++;
            }
            l = (l * main_vol_l) >> 15;
            r = (r * main_vol_r) >> 15;
            if (l > 32767)
                l = 32767;
            if (l < -32768)
                l = -32768;
            if (r > 32767)
                r = 32767;
            if (r < -32768)
                r = -32768;
            buf[i * 2] = (int16_t)l;
            buf[i * 2 + 1] = (int16_t)r;
        }
        plat_audio_queue(buf, n);
        frames -= n;
    }
}

/* called once per vblank */
void psx_spu_vblank(void)
{
    int frames;
    sample_accum += OUTPUT_RATE / 59.94;
    frames = (int)sample_accum;
    sample_accum -= frames;
    /* keep latency bounded if the host audio device stalls */
    if (plat_audio_queued_frames() > OUTPUT_RATE / 5)
        return;
    mix(frames);
}

/* called from the interrupt dispatcher */
void psx_spu_interrupt(void)
{
    if (irq_pending) {
        irq_pending = 0;
        if (irq_callback)
            irq_callback();
    }
}

/* ---- libspu API ------------------------------------------------------------------- */

static int16_t vol15(short v)
{
    int32_t x = (int32_t)v * 2;
    if (x > 32767) x = 32767;
    if (x < -32768) x = -32768;
    return (int16_t)x;
}

void psx_spu_init(void)
{
    memset(spu_ram, 0, sizeof(spu_ram));
    memset(voices, 0, sizeof(voices));
}

void SpuInit(void) { psx_spu_init(); }
void SpuQuit(void) {}

long SpuSetTransferMode(long mode) { (void)mode; return 0; }

unsigned long SpuSetTransferStartAddr(unsigned long addr)
{
    if (addr < 0x1010 || addr >= SPU_RAM_SIZE)
        return 0;
    transfer_addr = (uint32_t)addr;
    return addr;
}

unsigned long SpuWrite(unsigned char *addr, unsigned long size)
{
    unsigned long n = size;
    if (transfer_addr + n > SPU_RAM_SIZE)
        n = SPU_RAM_SIZE - transfer_addr;
    memcpy(&spu_ram[transfer_addr], addr, n);
    transfer_addr += (uint32_t)n;
    return size;
}

long SpuIsTransferCompleted(long flag) { (void)flag; return 1; }

void SpuSetVoiceAttr(SpuVoiceAttr *a)
{
    int i;
    for (i = 0; i < NUM_VOICES; i++) {
        Voice *v = &voices[i];
        u_long m = a->mask;
        if (!(a->voice & (1ul << i)))
            continue;
        if (m == 0)
            m = 0x7FFFF;
        if (m & V_VOLL) v->vol_l = vol15(a->volume.left);
        if (m & V_VOLR) v->vol_r = vol15(a->volume.right);
        if (m & V_PITCH) v->pitch = a->pitch;
        if (m & V_NOTE) {
            /* note: semitone in the high byte; pitch relative to sample_note */
            int d = ((a->note >> 8) - (v->sample_note >> 8)) * 128 + ((a->note & 0xFF) - (v->sample_note & 0xFF));
            double p = 4096.0 * exp2(d / (12.0 * 128.0));
            v->note = a->note;
            v->pitch = p > 0x3FFF ? 0x3FFF : (uint16_t)p;
        }
        if (m & V_SNOTE) v->sample_note = a->sample_note;
        if (m & V_WDSA) v->start = (uint32_t)a->addr & ~7u;
        if (m & V_LSAX) v->loop = (uint32_t)a->loop_addr & ~7u;
        if (m & V_ADSR1) v->adsr1 = a->adsr1;
        if (m & V_ADSR2) v->adsr2 = a->adsr2;
        if (m & V_AMODE) v->adsr1 = (uint16_t)((v->adsr1 & 0x7FFF) | (a->a_mode == 5 ? 0x8000 : 0));
        if (m & V_AR) v->adsr1 = (uint16_t)((v->adsr1 & 0x80FF) | ((a->ar & 0x7F) << 8));
        if (m & V_DR) v->adsr1 = (uint16_t)((v->adsr1 & 0xFF0F) | ((a->dr & 0xF) << 4));
        if (m & V_SL) v->adsr1 = (uint16_t)((v->adsr1 & 0xFFF0) | (a->sl & 0xF));
        if (m & V_SMODE) {
            uint16_t bits = 0;
            if (a->s_mode == 5 || a->s_mode == 7) bits |= 0x8000;      /* exponential */
            if (a->s_mode == 3 || a->s_mode == 4 || a->s_mode == 7) bits |= 0x4000; /* decrease */
            v->adsr2 = (uint16_t)((v->adsr2 & 0x3FFF) | bits);
        }
        if (m & V_SR) v->adsr2 = (uint16_t)((v->adsr2 & 0xE03F) | ((a->sr & 0x7F) << 6));
        if (m & V_RMODE) v->adsr2 = (uint16_t)((v->adsr2 & ~0x20) | (a->r_mode == 7 ? 0x20 : 0));
        if (m & V_RR) v->adsr2 = (uint16_t)((v->adsr2 & ~0x1F) | (a->rr & 0x1F));
    }
}

void SpuGetVoiceAttr(SpuVoiceAttr *a)
{
    int i;
    for (i = 0; i < NUM_VOICES; i++)
        if (a->voice & (1ul << i))
            break;
    if (i == NUM_VOICES)
        return;
    a->volume.left = (short)(voices[i].vol_l / 2);
    a->volume.right = (short)(voices[i].vol_r / 2);
    a->volumex = a->volume;
    a->pitch = voices[i].pitch;
    a->note = voices[i].note;
    a->sample_note = voices[i].sample_note;
    a->envx = (short)voices[i].env_level;
    a->addr = voices[i].start;
    a->loop_addr = voices[i].loop;
    a->adsr1 = voices[i].adsr1;
    a->adsr2 = voices[i].adsr2;
}

void SpuSetKey(long on_off, unsigned long voice_bit)
{
    int i;
    for (i = 0; i < NUM_VOICES; i++) {
        if (!(voice_bit & (1ul << i)))
            continue;
        if (on_off)
            key_on(&voices[i]);
        else
            key_off(&voices[i]);
    }
}

long SpuGetKeyStatus(unsigned long voice_bit)
{
    int i;
    for (i = 0; i < NUM_VOICES; i++) {
        if (voice_bit & (1ul << i)) {
            Voice *v = &voices[i];
            int env = v->env_phase != ENV_OFF && v->env_level > 0;
            if (v->keyed)
                return env ? 1 : 3;     /* SPU_ON / SPU_ON_ENV_OFF */
            return env ? 2 : 0;         /* SPU_OFF_ENV_ON / SPU_OFF */
        }
    }
    return -1;
}

void SpuGetAllKeysStatus(char *status)
{
    int i;
    for (i = 0; i < NUM_VOICES; i++)
        status[i] = (char)SpuGetKeyStatus(1ul << i);
}

void SpuSetCommonAttr(SpuCommonAttr *a)
{
    if (a->mask == 0 || (a->mask & C_MVOLL))
        main_vol_l = vol15(a->mvol.left);
    if (a->mask == 0 || (a->mask & C_MVOLR))
        main_vol_r = vol15(a->mvol.right);
    if (a->mask == 0 || (a->mask & C_CDVOLL))
        cd_vol_l = a->cd.volume.left;
    if (a->mask == 0 || (a->mask & C_CDVOLR))
        cd_vol_r = a->cd.volume.right;
    if (a->mask == 0 || (a->mask & C_CDMIX))
        cd_mix = a->cd.mix != 0;
}

/* ---- IRQ ------------------------------------------------------------------------ */

long SpuSetIRQ(long on_off)
{
    if (on_off == 3) /* SPU_RESET */
        on_off = 1;
    irq_enabled = on_off != 0;
    irq_pending = 0;
    return on_off;
}

unsigned long SpuSetIRQAddr(unsigned long addr)
{
    irq_addr = (uint32_t)addr;
    return addr;
}

SpuIRQCallbackProc SpuSetIRQCallback(SpuIRQCallbackProc func)
{
    SpuIRQCallbackProc old = irq_callback;
    irq_callback = func;
    return old;
}

/* ---- sound RAM allocator (SpuInitMalloc/SpuMalloc) ---------------------------------- */

#define MALLOC_MAX 64

static struct { uint32_t addr, size; int used; } blocks[MALLOC_MAX];
static int      nblocks;
static uint32_t heap_top = 0x1010;
static uint32_t heap_end = SPU_RAM_SIZE;

long SpuInitMalloc(long num, char *top)
{
    (void)num; (void)top;
    nblocks = 0;
    heap_top = 0x1010;
    return num;
}

long SpuMalloc(long size)
{
    int i;
    uint32_t sz = ((uint32_t)size + 15) & ~15u;
    for (i = 0; i < nblocks; i++) {
        if (!blocks[i].used && blocks[i].size >= sz) {
            blocks[i].used = 1;
            return (long)blocks[i].addr;
        }
    }
    if (heap_top + sz > heap_end || nblocks == MALLOC_MAX)
        return -1;
    blocks[nblocks].addr = heap_top;
    blocks[nblocks].size = sz;
    blocks[nblocks].used = 1;
    nblocks++;
    heap_top += sz;
    return (long)(heap_top - sz);
}

void SpuFree(unsigned long addr)
{
    int i;
    for (i = 0; i < nblocks; i++)
        if (blocks[i].addr == addr)
            blocks[i].used = 0;
}

/* ---- reverb, noise, LFO: state only (not mixed yet) ------------------------------------ */

static uint32_t reverb_voices;

long SpuSetReverb(long on_off) { return on_off; }
long SpuSetReverbModeParam(SpuReverbAttr *attr) { (void)attr; return 0; }
long SpuSetReverbDepth(SpuReverbAttr *attr) { (void)attr; return 0; }
long SpuReserveReverbWorkArea(long on_off)
{
    /* the work area sits at the top of sound RAM */
    heap_end = on_off ? SPU_RAM_SIZE - 0x10000 : SPU_RAM_SIZE;
    return on_off;
}
long SpuClearReverbWorkArea(long mode) { (void)mode; return 0; }

unsigned long SpuSetReverbVoice(long on_off, unsigned long voice_bit)
{
    if (on_off == 1)
        reverb_voices |= (uint32_t)voice_bit;
    else if (on_off == 0)
        reverb_voices &= ~(uint32_t)voice_bit;
    return reverb_voices;
}

unsigned long SpuSetNoiseVoice(long on_off, unsigned long voice_bit) { (void)on_off; return voice_bit; }
unsigned long SpuSetPitchLFOVoice(long on_off, unsigned long voice_bit) { (void)on_off; return voice_bit; }
