#!/usr/bin/env python3
"""
Static recompiler: MIPS R3000A machine code -> C for the native port.

Overlays that are not fully decompiled yet are recompiled from their
original binary (built bit-identical by the PS1 matching build), so the
native port can run every stage today. Decompiled code always wins: an
overlay is only recompiled while it still contains assembly.

How the generated code fits the native program:
- Each function becomes a C function on emulated registers; MIPS stack
  frames live in a local array, delay slots, jump tables, HI/LO, unaligned
  accesses and GTE (COP2) instructions are translated.
- The overlay image stays at its original address (0x800C3208, loaded by the
  game itself), so the overlay's own data is accessed as-is.
- Addresses of main executable symbols (functions and data) are translated to
  the native symbols. A forward data-flow analysis follows %hi/%lo pairs and
  $gp through the whole function to find them.
- Calls into the main executable call the native functions directly; calls
  through pointers work because the runtime places a small x86 jump at every
  recompiled entry point in the overlay's code area (psx_recomp_install).
- Words in the overlay's data that point at main executable symbols are
  relocated when the overlay is installed.

Usage:
  recomp.py overlay <name> <ovl.bin> <ovl.map> <native-syms> <runtime-syms> <headers-dir> <out.c>
  recomp.py exe <exe> <exe.map> <native-syms> <headers-dir> <out.c> <func>...

<native-syms> lists every symbol the native program has (one per line);
<runtime-syms> the functions of the PlayStation runtime: overlay copies of
statically linked PsyQ library functions with these names call the runtime
instead (they would otherwise drive hardware registers).
"""
import os
import re
import struct
import sys

MAIN_LO = 0x80010000

# native names of library functions the port renames (mgs_native.h)
RENAMES = {
    'rand': 'psx_rand', 'srand': 'psx_srand', 'open': 'psx_open', 'read': 'psx_read',
    'write': 'psx_write', 'close': 'psx_close', 'lseek': 'psx_lseek',
    'fprintf': 'mgs_fprintf', 'main': 'mgs_game_main', 'printf': 'printf',
}

GPR = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']
CALLER_SAVED = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 31}


# ---- inputs -----------------------------------------------------------------------

def parse_map(path):
    sections, syms = [], []
    with open(path, encoding='latin-1') as f:
        lines = f.read().splitlines()
    for l in lines[1:]:
        m = re.match(r'\s*([0-9A-F]{8})\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s+[0-9A-F]{8}\s+(\w+)\s+(\S+)', l)
        if m:
            sections.append((int(m[1], 16), int(m[2], 16) + 1, m[4], m[5]))
        elif sections:
            break
    start = next(i for i, l in enumerate(lines) if 'Names in address order' in l)
    for l in lines[start + 1:]:
        m = re.match(r'\s*([0-9A-F]{8})\s+(\S+)\s*$', l)
        if m:
            syms.append((int(m[1], 16), m[2]))
    return sections, syms


def is_pseudo(name):
    return name.startswith(('_overlay', '__', '_bss_', '_text', '_data', '_sdata', '_sbss', '_rdata', '_gp',
                            '_ramsize', '_stacksize', '_startTime', '_waitTime')) or name in ('_end',)


def narrow_returns(headers_dir):
    """Functions returning char/short: the native caller must extend them."""
    pat = re.compile(r'^\s*(?:extern\s+|static\s+|inline\s+)*'
                     r'(unsigned\s+char|unsigned\s+short|signed\s+char|u_char|u_short|short|char)\s+(\w+)\s*\(', re.M)
    out = {}
    for dp, _, files in os.walk(headers_dir):
        for fn in files:
            if fn.endswith(('.h', '.c')):
                with open(os.path.join(dp, fn), encoding='utf-8', errors='replace') as f:
                    for m in pat.finditer(f.read()):
                        t = m.group(1).replace('u_char', 'unsigned char').replace('u_short', 'unsigned short')
                        if t == 'char':
                            t = 'unsigned char'   # PsyQ GCC: char is unsigned
                        out[m.group(2)] = t
    return out


# ---- the program image ------------------------------------------------------------

class Image:
    def __init__(self, base, data):
        self.base, self.data = base, data

    def word(self, addr):
        o = addr - self.base
        if 0 <= o <= len(self.data) - 4:
            return struct.unpack_from('<I', self.data, o)[0]
        return None


def sext16(v):
    return v - 0x10000 if v & 0x8000 else v


# ---- recompiler ----------------------------------------------------------------------

class Recompiler:
    def __init__(self, image, text_lo, text_hi, syms, main_hi, native, narrow, prefix, runtime=()):
        self.img = image
        self.runtime = set(runtime)
        self.cur = None
        self.internal = set()
        self.inner = set()      # function starts inside merged regions (entries)
        self.own = None         # recompiled functions, if not the whole text
        self.text_lo, self.text_hi = text_lo, text_hi
        self.main_hi = main_hi
        self.native = native
        self.narrow = narrow
        self.prefix = prefix
        self.warnings = []
        self.externs = set()
        # main executable symbols for address translation
        self.main_syms = sorted((a, n) for a, n in syms if MAIN_LO <= a < main_hi and not is_pseudo(n))
        self.main_addrs = [a for a, _ in self.main_syms]
        self.sym_at = {}
        for a, n in syms:
            if not is_pseudo(n):
                self.sym_at.setdefault(a, n)
        self.gp = next((a for a, n in syms if n == '_gp'), None)

    # -- symbols -------------------------------------------------------------------
    def native_name(self, name):
        return RENAMES.get(name, name)

    def main_symbol(self, addr):
        """Main executable symbol containing addr (by start address)."""
        import bisect
        i = bisect.bisect_right(self.main_addrs, addr) - 1
        if i < 0:
            return None
        a, n = self.main_syms[i]
        return a, n

    def in_main(self, addr):
        return MAIN_LO <= addr < self.main_hi

    def reloc_expr(self, addr):
        """C expression (uint32_t) for main executable address addr, or None."""
        s = self.main_symbol(addr)
        if not s:
            return None
        a, n = s
        nn = self.native_name(n)
        if nn not in self.native:
            self.warnings.append('no native symbol for %s (0x%08X)' % (n, addr))
            return None
        self.externs.add(nn)
        if addr == a:
            return '((uint32_t)(uintptr_t)%s)' % nn
        return '((uint32_t)(uintptr_t)%s + 0x%Xu)' % (nn, addr - a)

    def reloc_delta(self, addr):
        """C expression to add to a raw PS1 address inside a main symbol."""
        s = self.main_symbol(addr)
        if not s:
            return None
        a, n = s
        nn = self.native_name(n)
        if nn not in self.native:
            self.warnings.append('no native symbol for %s (0x%08X)' % (n, addr))
            return None
        self.externs.add(nn)
        return '((uint32_t)(uintptr_t)%s - 0x%08Xu)' % (nn, a)

    # -- decoding helpers ----------------------------------------------------------
    @staticmethod
    def fields(w):
        return (w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63, w & 0xFFFF)

    def branch_target(self, pc, w):
        return (pc + 4 + (sext16(w & 0xFFFF) << 2)) & 0xFFFFFFFF

    def jump_target(self, pc, w):
        return ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)

    def kind(self, w):
        """'branch', 'jump', 'jal', 'jr', 'jalr' or None"""
        op, rs, rt, rd, sa, fn, imm = self.fields(w)
        if op == 0 and fn == 8:
            return 'jr'
        if op == 0 and fn == 9:
            return 'jalr'
        if op == 2:
            return 'jump'
        if op == 3:
            return 'jal'
        if op in (4, 5, 6, 7) or (op == 1 and rt in (0, 1, 16, 17)):
            return 'branch'
        return None

    def dest(self, w):
        """Register written by the instruction (None if none)."""
        op, rs, rt, rd, sa, fn, imm = self.fields(w)
        if op == 0:
            if fn in (0, 2, 3, 4, 6, 7, 16, 18, 32, 33, 34, 35, 36, 37, 38, 39, 42, 43):
                return rd
            if fn == 9:
                return rd
            return None
        if op == 1 and rt in (16, 17):
            return 31
        if op == 3:
            return 31
        if op in (8, 9, 10, 11, 12, 13, 14, 15, 32, 33, 34, 35, 36, 37, 38):
            return rt
        if op == 18 and not (w & (1 << 25)) and rs in (0, 2):
            return rt
        if op == 16 and rs == 0:
            return rt
        return None

    # -- function discovery --------------------------------------------------------
    def discover(self, extra_entries=()):
        lo, hi = self.text_lo, self.text_hi
        primary = set(a for a, n in self.sym_at.items() if lo <= a < hi)
        primary.update(extra_entries)
        pointers = set()
        for pc in range(lo, hi, 4):
            w = self.img.word(pc)
            if w is None:
                continue
            if self.kind(w) == 'jal':
                t = self.jump_target(pc, w)
                if lo <= t < hi:
                    primary.add(t)
        # code constants pointing into the text are function pointers
        for pc in range(lo, hi, 4):
            w = self.img.word(pc)
            if w is None:
                continue
            op, rs, rt, rd, sa, fn, imm = self.fields(w)
            if op == 15:   # lui; look at the next few instructions for the %lo part
                for k in range(1, 6):
                    w2 = self.img.word(pc + 4 * k)
                    if w2 is None:
                        break
                    op2, rs2, rt2, _, _, _, imm2 = self.fields(w2)
                    if op2 == 9 and rs2 == rt:
                        t = ((imm << 16) + sext16(imm2)) & 0xFFFFFFFF
                        if lo <= t < hi and t % 4 == 0:
                            primary.add(t)
                        break
        # data words pointing into the text: jump table targets (labels), or
        # functions referenced from data tables when they follow a "jr ra"
        base, data = self.img.base, self.img.data
        labels, secondary = set(), set()
        for o in range(0, len(data) - 3, 4):
            v = struct.unpack_from('<I', data, o)[0]
            a = base + o
            if lo <= a < hi or not (lo <= v < hi) or v % 4:
                continue
            labels.add(v)
            if v not in primary and self.img.word(v - 8) == 0x03E00008:
                secondary.add(v)
        self.primary = sorted(primary)
        self.pointers = labels          # jump targets inside regions
        self.secondary = secondary      # entry points inside regions (get thunks)

    def find_internal(self, regions):
        """Jumps and branches into the middle of another region (code shared by
        cross-jumping): such targets become entries without a thunk."""
        starts = set(s for s, _ in regions)
        internal = set()
        for s, e in regions:
            for pc in range(s, e, 4):
                w = self.img.word(pc)
                k = self.kind(w)
                if k == 'branch':
                    t = self.branch_target(pc, w)
                elif k == 'jump':
                    t = self.jump_target(pc, w)
                else:
                    continue
                if not (s <= t < e) and self.text_lo <= t < self.text_hi and t not in starts:
                    internal.add(t)
        self.internal = internal - self.secondary

    def regions(self):
        starts = self.primary
        out = []
        for i, s in enumerate(starts):
            e = starts[i + 1] if i + 1 < len(starts) else self.text_hi
            out.append((s, e))
        return self.merge_cross_jumps(out)

    def is_forwarded(self, start):
        n = self.sym_at.get(start)
        return bool(n) and self.native_name(n) in self.runtime and self.native_name(n) in self.native

    def merge_cross_jumps(self, regs):
        """Functions that jump (j/b, not jal) into another one share code with
        it, typically an epilogue that relies on the jumping function's stack
        frame and registers. Such functions become one C function, so the jump
        stays a goto; the real function starts inside become its entries."""
        import bisect
        starts = [s for s, _ in regs]
        parent = list(range(len(regs)))

        def find(i):
            while parent[i] != i:
                parent[i] = parent[parent[i]]
                i = parent[i]
            return i

        for i, (s, e) in enumerate(regs):
            for pc in range(s, e, 4):
                w = self.img.word(pc)
                k = self.kind(w)
                if k == 'branch':
                    t = self.branch_target(pc, w)
                elif k == 'jump':
                    t = self.jump_target(pc, w)
                else:
                    continue
                if s <= t < e or not (self.text_lo <= t < self.text_hi):
                    continue
                j = bisect.bisect_right(starts, t) - 1
                if j >= 0 and t != starts[j]:           # into the middle of region j
                    parent[find(i)] = find(j)
        groups = {}
        for i in range(len(regs)):
            groups.setdefault(find(i), []).append(i)
        spans = []
        for g in groups.values():
            if len(g) == 1:
                continue
            if any(self.is_forwarded(regs[i][0]) for i in g):
                self.warnings.append('cross-jump into a library function at 0x%08X not merged' % regs[g[0]][0])
                continue
            spans.append([min(regs[i][0] for i in g), max(regs[i][1] for i in g)])
        spans.sort()
        merged = []
        for sp in spans:                                # overlapping spans become one
            if merged and sp[0] < merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], sp[1])
            else:
                merged.append(sp)
        if not merged:
            return regs
        out = []
        for s, e in regs:
            m = next((sp for sp in merged if sp[0] <= s < sp[1]), None)
            if m is None:
                out.append((s, e))
            elif s == m[0]:
                out.append((m[0], m[1]))
            else:
                self.inner.add(s)                       # a function start inside a merged span
        return out

    # -- data flow: known main-executable addresses in registers ---------------------
    def transfer(self, w, st):
        """Static PS1 address part per register after instruction w."""
        st = dict(st)
        op, rs, rt, rd, sa, fn, imm = self.fields(w)
        if op == 15:                                   # lui
            st[rt] = imm << 16
            return st
        if op in (9, 13) and rs in st:                 # addiu / ori: completes an address
            v = (st[rs] + sext16(imm)) & 0xFFFFFFFF if op == 9 else (st[rs] | imm)
            if self.in_main(v):
                st.pop(rt, None)                       # now holds the native address
            else:
                st[rt] = v
            return st
        if op == 0 and fn in (32, 33) and (rs in st) != (rt in st):   # base + index
            st[rd] = st[rs] if rs in st else st[rt]
            return st
        d = self.dest(w)
        if d is not None:
            st.pop(d, None)
        return st

    def analyse(self, start, end, labels, entries):
        """IN state (reg -> static address part) for every instruction."""
        leaders = {start} | set(l for l in labels if start <= l < end) | set(entries)
        for pc in range(start, end, 4):
            if self.kind(self.img.word(pc)):
                leaders.add(pc + 8)
        leaders = set(l for l in leaders if start <= l < end)
        order = sorted(leaders)
        entry_state = {28: self.gp} if self.gp is not None else {}
        block_in = {e: dict(entry_state) for e in entries}

        def join(a, b):
            if a is None:
                return dict(b)
            return {r: v for r, v in a.items() if b.get(r) == v}

        def run_block(lead, st, record=None):
            pc = lead
            while pc < end:
                if record is not None:
                    record[pc] = st
                w = self.img.word(pc)
                k = self.kind(w)
                if k:
                    st = self.transfer(w, st)
                    if record is not None:
                        record[pc + 4] = st
                    st = self.transfer(self.img.word(pc + 4), st)   # delay slot
                    if k in ('jal', 'jalr'):
                        for r in CALLER_SAVED:
                            st.pop(r, None)
                        return st, [pc + 8]
                    if k == 'branch':
                        return st, [self.branch_target(pc, w), pc + 8]
                    if k == 'jump':
                        return st, [self.jump_target(pc, w)]
                    if k == 'jr':
                        return st, ([] if ((w >> 21) & 31) == 31 else sorted(labels))
                st = self.transfer(w, st)
                pc += 4
                if pc in leaders:
                    return st, [pc]
            return st, []

        work = list(block_in)
        while work:
            lead = work.pop()
            out, succ = run_block(lead, block_in[lead])
            for s2 in succ:
                if not (start <= s2 < end) or s2 not in leaders:
                    continue
                new = join(block_in.get(s2), out)
                if s2 not in block_in or new != block_in[s2]:
                    block_in[s2] = new
                    work.append(s2)
        per_pc = {}
        for lead, st in block_in.items():
            run_block(lead, st, per_pc)
        return per_pc

    # -- code generation -----------------------------------------------------------
    def R(self, n):
        return '0u' if n == 0 else 'r%d' % n

    def addr(self, base, off, st):
        """Address expression for off(base); translates main executable references."""
        expr = '(%s + 0x%Xu)' % (self.R(base), off & 0xFFFFFFFF) if off else self.R(base)
        if st and base in st:
            s = (st[base] + off) & 0xFFFFFFFF
            if 0x1F801000 <= s < 0x1F803000:
                self.warnings.append('hardware register 0x%08X accessed in %s' % (s, self.sym_at.get(self.cur, '%08X' % self.cur)))
            if self.in_main(s):
                d = self.reloc_delta(s)
                if d:
                    return '(%s + %s)' % (expr, d)
        return expr

    def call_expr(self, target):
        """C call expression for a jal to target (v0 = result)."""
        args = 'r4, r5, r6, r7, M32(r29 + 16), M32(r29 + 20), M32(r29 + 24), M32(r29 + 28)'
        if self.text_lo <= target < self.text_hi and (self.own is None or target in self.own):
            return '%s_%08X(%s)' % (self.prefix, target, args)
        if self.in_main(target):
            n = self.sym_at.get(target)
            if n:
                nn = self.native_name(n)
                if nn in self.native:
                    self.externs.add(nn)
                    t = self.narrow.get(n)
                    call = '((RcFn)%s)(%s)' % (nn, args)
                    if t:
                        return '(uint32_t)(int32_t)(%s)%s' % (t, call)
                    return '(uint32_t)%s' % call
                self.warnings.append('call to %s has no native counterpart' % n)
                return 'rc_unresolved(0x%08Xu, "%s")' % (target, n)
        self.warnings.append('call to unknown address 0x%08X' % target)
        return 'rc_unresolved(0x%08Xu, "?")' % target

    def gen_insn(self, pc, w, st, labels, start, end):
        """C statements for one non-control-flow instruction."""
        op, rs, rt, rd, sa, fn, imm = self.fields(w)
        R = self.R
        si = sext16(imm)

        def setr(r, e):
            return '' if r == 0 else '%s = %s;' % (R(r), e)

        if w == 0:
            return ''
        if op == 0:
            if fn == 0: return setr(rd, '%s << %d' % (R(rt), sa))
            if fn == 2: return setr(rd, '%s >> %d' % (R(rt), sa))
            if fn == 3: return setr(rd, '(uint32_t)((int32_t)%s >> %d)' % (R(rt), sa))
            if fn == 4: return setr(rd, '%s << (%s & 31)' % (R(rt), R(rs)))
            if fn == 6: return setr(rd, '%s >> (%s & 31)' % (R(rt), R(rs)))
            if fn == 7: return setr(rd, '(uint32_t)((int32_t)%s >> (%s & 31))' % (R(rt), R(rs)))
            if fn == 12: return 'rc_syscall(0x%08Xu);' % pc
            if fn == 13: return ''
            if fn == 16: return setr(rd, 'hi')
            if fn == 17: return 'hi = %s;' % R(rs)
            if fn == 18: return setr(rd, 'lo')
            if fn == 19: return 'lo = %s;' % R(rs)
            if fn == 24: return '{ int64_t p = (int64_t)(int32_t)%s * (int32_t)%s; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); }' % (R(rs), R(rt))
            if fn == 25: return '{ uint64_t p = (uint64_t)%s * %s; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); }' % (R(rs), R(rt))
            if fn == 26: return 'rc_div(%s, %s, &hi, &lo);' % (R(rs), R(rt))
            if fn == 27: return 'rc_divu(%s, %s, &hi, &lo);' % (R(rs), R(rt))
            if fn in (32, 33):
                if st and (rs in st) != (rt in st):
                    pass
                return setr(rd, '%s + %s' % (R(rs), R(rt)))
            if fn in (34, 35): return setr(rd, '%s - %s' % (R(rs), R(rt)))
            if fn == 36: return setr(rd, '%s & %s' % (R(rs), R(rt)))
            if fn == 37: return setr(rd, '%s | %s' % (R(rs), R(rt)))
            if fn == 38: return setr(rd, '%s ^ %s' % (R(rs), R(rt)))
            if fn == 39: return setr(rd, '~(%s | %s)' % (R(rs), R(rt)))
            if fn == 42: return setr(rd, '((int32_t)%s < (int32_t)%s)' % (R(rs), R(rt)))
            if fn == 43: return setr(rd, '(%s < %s)' % (R(rs), R(rt)))
        if op in (8, 9):
            if st and rs in st:
                v = (st[rs] + si) & 0xFFFFFFFF
                if self.in_main(v):
                    e = self.reloc_expr(v)
                    if e:
                        return setr(rt, e)
            return setr(rt, '%s + 0x%Xu' % (R(rs), si & 0xFFFFFFFF))
        if op == 10: return setr(rt, '((int32_t)%s < %d)' % (R(rs), si))
        if op == 11: return setr(rt, '(%s < 0x%Xu)' % (R(rs), si & 0xFFFFFFFF))
        if op == 12: return setr(rt, '%s & 0x%Xu' % (R(rs), imm))
        if op == 13:
            if st and rs in st:
                v = st[rs] | imm
                if self.in_main(v):
                    e = self.reloc_expr(v)
                    if e:
                        return setr(rt, e)
            return setr(rt, '%s | 0x%Xu' % (R(rs), imm))
        if op == 14: return setr(rt, '%s ^ 0x%Xu' % (R(rs), imm))
        if op == 15: return setr(rt, '0x%08Xu' % (imm << 16))
        if op in (32, 33, 34, 35, 36, 37, 38, 40, 41, 42, 43, 46, 50, 58):
            a = self.addr(rs, si, st)
            # the stack frame and addresses with a known upper half can never
            # be null pointers: access them directly (N*), the rest through
            # the console-like mapping (M*)
            known = rs == 29 or (st and rs in st and 0x10000 <= st[rs] < 0xFFFF0000)
            m = 'N' if known else 'M'
            if op == 32: return setr(rt, '(uint32_t)(int32_t)(int8_t)%s8(%s)' % (m, a))
            if op == 33: return setr(rt, '(uint32_t)(int32_t)(int16_t)%s16(%s)' % (m, a))
            if op == 34: return setr(rt, 'rc_lwl(%s, %s)' % (R(rt), a))
            if op == 35: return setr(rt, '%s32(%s)' % (m, a))
            if op == 36: return setr(rt, '%s8(%s)' % (m, a))
            if op == 37: return setr(rt, '%s16(%s)' % (m, a))
            if op == 38: return setr(rt, 'rc_lwr(%s, %s)' % (R(rt), a))
            if op == 40: return '%s8(%s) = (uint8_t)%s;' % (m, a, R(rt))
            if op == 41: return '%s16(%s) = (uint16_t)%s;' % (m, a, R(rt))
            if op == 42: return 'rc_swl(%s, %s);' % (a, R(rt))
            if op == 43: return '%s32(%s) = %s;' % (m, a, R(rt))
            if op == 46: return 'rc_swr(%s, %s);' % (a, R(rt))
            if op == 50: return 'psx_gte_write_data(%d, %s32(%s));' % (rt, m, a)
            if op == 58: return '%s32(%s) = psx_gte_read_data(%d);' % (m, a, rt)
        if op == 16:   # COP0
            if rs == 0: return setr(rt, '0u')
            return ''
        if op == 18:   # COP2 (GTE)
            if w & (1 << 25):
                return 'psx_gte_command(0x%Xu);' % (w & 0x1FFFFFF)
            if rs == 0: return setr(rt, 'psx_gte_read_data(%d)' % rd)
            if rs == 2: return setr(rt, 'psx_gte_read_ctrl(%d)' % rd)
            if rs == 4: return 'psx_gte_write_data(%d, %s);' % (rd, R(rt))
            if rs == 6: return 'psx_gte_write_ctrl(%d, %s);' % (rd, R(rt))
        self.warnings.append('unknown instruction %08X at %08X' % (w, pc))
        return 'rc_unknown(0x%08Xu, 0x%08Xu);' % (pc, w)

    def gen_forward(self, start, end, native):
        """A statically linked PsyQ library function the runtime implements."""
        self.externs.add(native)
        out = ['/* %s: PsyQ library function, implemented by the runtime */' % native]
        out.append('uint32_t %s_%08X()' % (self.prefix, start))
        t = self.narrow.get(native)
        call = '((RcFn)%s)(RC_ARGS8)' % native
        out.append('{ return %s; }' % ('(uint32_t)(int32_t)(%s)%s' % (t, call) if t else call))
        entries = [start]
        for e in sorted(p for p in self.secondary | self.internal | self.inner if start < p < end):
            self.warnings.append('entry 0x%08X inside library function %s' % (e, native))
            out.append('uint32_t %s_%08X()' % (self.prefix, e))
            out.append('{ return rc_unresolved(0x%08Xu, "%s+"); }' % (e, native))
            entries.append(e)
        return out, entries

    def gen_function(self, start, end):
        self.cur = start
        n = self.sym_at.get(start)
        if n and self.native_name(n) in self.runtime and self.native_name(n) in self.native:
            return self.gen_forward(start, end, self.native_name(n))
        lab = set()
        for pc in range(start, end, 4):
            w = self.img.word(pc)
            k = self.kind(w)
            if k == 'branch':
                t = self.branch_target(pc, w)
                if start <= t < end:
                    lab.add(t)
            elif k == 'jump':
                t = self.jump_target(pc, w)
                if start <= t < end:
                    lab.add(t)
        lab |= set(p for p in self.pointers if start < p < end)
        entries = [start] + sorted(p for p in self.secondary | self.internal | self.inner if start < p < end)
        lab |= set(entries[1:])
        st_at = self.analyse(start, end, lab, entries)

        frame = 64
        for pc in range(start, end, 4):
            w = self.img.word(pc)
            op, rs, rt, rd, sa, fn, imm = self.fields(w)
            if op == 9 and rs == 29 and rt == 29 and sext16(imm) < 0:
                frame = max(frame, -sext16(imm) + 64)
        frame_words = (frame + 64 + 3) // 4

        name = '%s_impl_%08X' % (self.prefix, start)
        out = []
        out.append('static uint32_t %s(int entry, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,'
                   ' uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)' % name)
        out.append('{')
        out.append('    uint32_t frame[%d];' % frame_words)
        out.append('    uint32_t %s;' % ', '.join('r%d = 0' % i for i in range(1, 32)))
        out.append('    uint32_t hi = 0, lo = 0, cond = 0, tgt = 0;')
        out.append('    (void)cond; (void)tgt; (void)hi; (void)lo;')
        out.append('    r29 = (uint32_t)(uintptr_t)&frame[%d];' % (frame_words - 16))
        if self.gp is not None:
            out.append('    r28 = 0x%08Xu;' % self.gp)
        out.append('    r4 = a0; r5 = a1; r6 = a2; r7 = a3;')
        out.append('    M32(r29 + 16) = a4; M32(r29 + 20) = a5; M32(r29 + 24) = a6; M32(r29 + 28) = a7;')
        out.append('    switch (entry) {')
        for i, e in enumerate(entries):
            out.append('    case %d: goto L_%08X;' % (i, e))
        out.append('    }')
        out.append('    return 0;')

        def jump_out(target):
            """Leaving the region: tail call."""
            if self.text_lo <= target < self.text_hi or self.in_main(target):
                return 'return %s;' % self.call_expr(target)
            return 'return rc_unresolved(0x%08Xu, "jump");' % target

        def computed_jump():
            """jr to a register: a jump table inside the region, or a tail call."""
            cases = ' '.join('case 0x%08Xu: goto L_%08X;' % (t, t) for t in sorted(lab))
            return ('switch (tgt) { %s default: return ((RcFn)(uintptr_t)tgt)(r4, r5, r6, r7, '
                    'M32(r29 + 16), M32(r29 + 20), M32(r29 + 24), M32(r29 + 28)); }' % cases)

        pc = start
        while pc < end:
            w = self.img.word(pc)
            st = st_at.get(pc, {})
            out.append('L_%08X:' % pc)
            k = self.kind(w)
            if not k:
                s = self.gen_insn(pc, w, st, lab, start, end)
                if s:
                    out.append('    ' + s)
                pc += 4
                continue
            op, rs, rt, rd, sa, fn, imm = self.fields(w)
            dw = self.img.word(pc + 4)
            dst = st_at.get(pc + 4, st)
            delay = self.gen_insn(pc + 4, dw, dst, lab, start, end) if dw is not None else ''
            R = self.R
            if k == 'branch':
                t = self.branch_target(pc, w)
                if op == 4: c = '%s == %s' % (R(rs), R(rt))
                elif op == 5: c = '%s != %s' % (R(rs), R(rt))
                elif op == 6: c = '(int32_t)%s <= 0' % R(rs)
                elif op == 7: c = '(int32_t)%s > 0' % R(rs)
                elif rt in (0, 16): c = '(int32_t)%s < 0' % R(rs)
                else: c = '(int32_t)%s >= 0' % R(rs)
                out.append('    cond = (%s);' % c)
                if rt in (16, 17) and op == 1:
                    out.append('    r31 = 0x%08Xu;' % (pc + 8))
                if delay:
                    out.append('    ' + delay)
                if start <= t < end:
                    out.append('    if (cond) goto L_%08X;' % t)
                else:
                    out.append('    if (cond) { %s }' % jump_out(t))
            elif k == 'jump':
                t = self.jump_target(pc, w)
                if delay:
                    out.append('    ' + delay)
                if start <= t < end:
                    out.append('    goto L_%08X;' % t)
                else:
                    out.append('    ' + jump_out(t))
            elif k == 'jal':
                t = self.jump_target(pc, w)
                if delay:
                    out.append('    ' + delay)
                out.append('    r2 = %s;' % self.call_expr(t))
            elif k == 'jalr':
                out.append('    tgt = %s;' % R(rs))
                if delay:
                    out.append('    ' + delay)
                out.append('    r2 = ((RcFn)(uintptr_t)tgt)(r4, r5, r6, r7, M32(r29 + 16), M32(r29 + 20), '
                           'M32(r29 + 24), M32(r29 + 28));')
            elif k == 'jr':
                if rs == 31:
                    if delay:
                        out.append('    ' + delay)
                    out.append('    return r2;')
                else:
                    out.append('    tgt = %s;' % R(rs))
                    if delay:
                        out.append('    ' + delay)
                    out.append('    ' + computed_jump())
            out.append('L_%08X:' % (pc + 4))
            pc += 8
        out.append('    return r2;')
        out.append('}')
        # entry wrappers
        for i, e in enumerate(entries):
            out.append('uint32_t %s_%08X()' % (self.prefix, e))
            out.append('{ return %s(%d, RC_ARGS8); }' % (name, i))
        return out, entries


HEADER = r'''/* Generated by port/tools/recomp.py - do not edit */
#include <stdint.h>

typedef uint32_t (*RcFn)();

/* Entry wrappers declare no parameters and only read their arguments: native
 * code calls them through function pointers with as many arguments as the
 * original function had, and a compiled function may reuse the stack slots
 * of its parameters - with eight declared ones it would overwrite the
 * caller's frame (a smashed return address in the actor loop). */
#define RC_ARG(i) (((const volatile uint32_t *)__builtin_frame_address(0))[2 + (i)])
#define RC_ARGS8 RC_ARG(0), RC_ARG(1), RC_ARG(2), RC_ARG(3), RC_ARG(4), RC_ARG(5), RC_ARG(6), RC_ARG(7)

/* Memory accesses as the console resolves them: the first 64 KiB (null
 * pointers; on the PS1 the start of RAM, e.g. the kernel area) go to the PS1
 * RAM window instead of faulting. Branch-free: a - 0x10000 has its top bit
 * set exactly for a < 0x10000 and for a >= 0x80010000 (top bit set already). */
static inline uintptr_t rc_ea(uint32_t a)
{
    return a | ((a - 0x10000u) & 0x80000000u);
}

#define M8(a)  (*(uint8_t *)rc_ea(a))
#define M16(a) (*(uint16_t *)rc_ea(a))
#define M32(a) (*(uint32_t *)rc_ea(a))
#define N8(a)  (*(uint8_t *)(uintptr_t)(a))
#define N16(a) (*(uint16_t *)(uintptr_t)(a))
#define N32(a) (*(uint32_t *)(uintptr_t)(a))

unsigned int psx_gte_read_data(int reg);
void         psx_gte_write_data(int reg, unsigned int value);
unsigned int psx_gte_read_ctrl(int reg);
void         psx_gte_write_ctrl(int reg, unsigned int value);
void         psx_gte_command(unsigned int cmd);
uint32_t     rc_unresolved(uint32_t addr, const char *what);
void         rc_unknown(uint32_t pc, uint32_t insn);
void         rc_syscall(uint32_t pc);

static inline void rc_div(uint32_t a, uint32_t b, uint32_t *hi, uint32_t *lo)
{
    int32_t n = (int32_t)a, d = (int32_t)b;
    if (d == 0) { *hi = a; *lo = n >= 0 ? 0xFFFFFFFFu : 1u; return; }
    if (n == INT32_MIN && d == -1) { *hi = 0; *lo = a; return; }
    *lo = (uint32_t)(n / d);
    *hi = (uint32_t)(n % d);
}

static inline void rc_divu(uint32_t a, uint32_t b, uint32_t *hi, uint32_t *lo)
{
    if (b == 0) { *hi = a; *lo = 0xFFFFFFFFu; return; }
    *lo = a / b;
    *hi = a % b;
}

static inline uint32_t rc_lwl(uint32_t rt, uint32_t a)
{
    uint32_t s = (a & 3) * 8, w = M32(a & ~3u);
    return (rt & (0x00FFFFFFu >> s)) | (w << (24 - s));
}

static inline uint32_t rc_lwr(uint32_t rt, uint32_t a)
{
    uint32_t s = (a & 3) * 8, w = M32(a & ~3u);
    return (rt & (0xFFFFFF00u << (24 - s))) | (w >> s);
}

static inline void rc_swl(uint32_t a, uint32_t rt)
{
    uint32_t s = (a & 3) * 8, w = M32(a & ~3u);
    M32(a & ~3u) = (w & (0xFFFFFF00u << s)) | (rt >> (24 - s));
}

static inline void rc_swr(uint32_t a, uint32_t rt)
{
    uint32_t s = (a & 3) * 8, w = M32(a & ~3u);
    M32(a & ~3u) = (w & (0x00FFFFFFu >> (24 - s))) | (rt << s);
}

typedef struct { uint32_t addr; RcFn fn; } RcEntry;
typedef struct { uint32_t addr; void *value; } RcReloc;
typedef struct RcOverlay {
    const char    *name;
    uint32_t       base, size;      /* overlay image */
    uint32_t       text, text_end;
    const RcEntry *entries;
    int            n_entries;
    const RcReloc *relocs;
    int            n_relocs;
} RcOverlay;
'''


def load_native(path):
    with open(path) as f:
        return set(l.strip() for l in f if l.strip())


def main():
    mode = sys.argv[1]
    if mode == 'overlay':
        name, binp, mapp, natp, rtp, hdrs, out = sys.argv[2:9]
        sections, syms = parse_map(mapp)
        ov = [s for s in sections if s[2] == 'overlay']
        base = min(s[0] for s in ov)
        text = next(s for s in ov if s[3] == 'overlay.text')
        main_hi = base
        with open(binp, 'rb') as f:
            data = f.read()
        img = Image(base, data)
        prefix = 'rc_%s' % name
        rc = Recompiler(img, text[0], text[1], syms, main_hi, load_native(natp), narrow_returns(hdrs), prefix,
                        load_native(rtp))
        rc.discover()
    else:
        exep, mapp, natp, hdrs, out = sys.argv[2:7]
        funcs = sys.argv[7:]
        sections, syms = parse_map(mapp)
        with open(exep, 'rb') as f:
            exe = f.read()
        t_addr, t_size = struct.unpack_from('<II', exe, 0x18)
        img = Image(t_addr, exe[0x800:0x800 + t_size])
        main_hi = max(s[1] for s in sections if s[2] in ('text', 'bss'))
        text = next(s for s in sections if s[3] == '.text')
        prefix = 'rc_exe'
        rc = Recompiler(img, text[0], text[1], syms, main_hi, load_native(natp), narrow_returns(hdrs), prefix)
        addrs = {n: a for a, n in syms}
        wanted = [addrs[f] for f in funcs]
        rc.own = set(wanted)    # everything else calls the native functions
        # only the requested functions (each up to the next symbol)
        allstarts = sorted(set(a for a, n in syms if text[0] <= a < text[1]))
        rc.primary = allstarts
        rc.pointers = set()
        rc.secondary = set()
        rc.internal = set()
        regions = []
        for a in wanted:
            i = allstarts.index(a)
            regions.append((a, allstarts[i + 1]))

    body, all_entries = [], []
    regs = rc.regions() if mode == 'overlay' else regions
    if mode == 'overlay':
        rc.find_internal(regs)
    # prototypes first (functions call each other)
    protos = []
    for s, e in regs:
        ents = [s] + sorted(p for p in rc.secondary | rc.internal | rc.inner if s < p < e)
        for x in ents:
            protos.append('uint32_t %s_%08X();' % (rc.prefix, x))
    for s, e in regs:
        code, ents = rc.gen_function(s, e)
        body += code
        all_entries += ents

    lines = [HEADER]
    for n in sorted(rc.externs):
        lines.append('extern char %s[];' % n)
    lines += protos
    lines += body

    if mode == 'overlay':
        # relocate data words pointing at main executable symbols
        relocs = []
        for o in range(0, len(img.data) - 3, 4):
            v = struct.unpack_from('<I', img.data, o)[0]
            a = img.base + o
            if text[0] <= a < text[1]:
                continue
            if rc.in_main(v) and v in rc.sym_at:
                e = rc.reloc_expr(v)
                if e:
                    n = rc.native_name(rc.sym_at[v])
                    relocs.append((a, n))
        for n in sorted(rc.externs):
            if 'extern char %s[];' % n not in lines:
                lines.insert(1, 'extern char %s[];' % n)
        lines.append('static const RcEntry entries[] = {')
        thunked = [e for e in all_entries if e not in rc.internal]
        for e in thunked:
            lines.append('    { 0x%08Xu, (RcFn)%s_%08X },' % (e, rc.prefix, e))
        lines.append('};')
        lines.append('static const RcReloc relocs[] = {')
        for a, n in relocs:
            lines.append('    { 0x%08Xu, (void *)%s },' % (a, n))
        lines.append('    { 0, 0 }')
        lines.append('};')
        lines.append('const RcOverlay rc_overlay_%s = { "%s", 0x%08Xu, %du, 0x%08Xu, 0x%08Xu, entries, %d, relocs, %d };'
                     % (name, name, base, len(data), text[0], text[1], len(thunked), len(relocs)))
    if mode == 'exe':
        # named aliases for the tests
        for f_, a in zip(funcs, wanted):
            lines.append('uint32_t rc_exe_fn_%s(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)' % f_)
            lines.append('{ return rc_exe_%08X(a0, a1, a2, a3, 0, 0, 0, 0); }' % a)
    with open(out, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    uniq = sorted(set(rc.warnings))
    for w in uniq[:30]:
        print('recomp: warning: %s' % w, file=sys.stderr)
    if len(uniq) > 30:
        print('recomp: ... %d more warnings' % (len(uniq) - 30), file=sys.stderr)
    print('recomp: %s: %d functions, %d entry points' % (out, len(regs), len(all_entries)))


if __name__ == '__main__':
    main()
