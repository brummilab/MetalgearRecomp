#!/usr/bin/env python3
"""
Translate the MIPS inline assembly used by the MGS sources into portable C.

The PS1 code talks to the GTE (geometry coprocessor) through GCC inline asm
(`lwc2`, `mtc2`, `cop2 0x...` ...). For the native port every such statement
is rewritten into calls to the GTE emulator (port/src/psx/gte.c). Register
bindings like `register int x asm("v1")` and empty `asm("")` scheduling hacks
are dropped, and anything touching the stack pointer ($29) becomes a no-op.

Usage: asm2c.py <out_dir> [?]<src_root>:<dst_subdir> [...]
A leading '?' marks a tree (the PsyQ SDK headers) where untranslatable files,
e.g. the DMPSX-only inline_c.h, are copied unchanged instead of failing.
Only files whose translated content changed are rewritten, so ninja keeps
incremental builds.
"""
import os
import re
import sys

REG_NAMES = {
    'zero': 0, 'at': 1, 'v0': 2, 'v1': 3, 'a0': 4, 'a1': 5, 'a2': 6, 'a3': 7,
    't0': 8, 't1': 9, 't2': 10, 't3': 11, 't4': 12, 't5': 13, 't6': 14, 't7': 15,
    's0': 16, 's1': 17, 's2': 18, 's3': 19, 's4': 20, 's5': 21, 's6': 22, 's7': 23,
    't8': 24, 't9': 25, 'k0': 26, 'k1': 27, 'gp': 28, 'sp': 29, 'fp': 30, 's8': 30, 'ra': 31,
}
SP = 29

# Small, targeted source fixups that the native build needs on top of the
# asm translation: (path suffix, regex, replacement). Keep this list short -
# real porting work belongs in port/src, not in rewritten upstream files.
# Applied to every source where they match. The PS1 compiler evaluates
# function arguments left to right; GCC on x86 may not (C leaves the order
# unspecified). Calls whose arguments read the script (GCL_*) one after the
# other are rewritten into statement expressions with that order fixed.
GLOBAL_FIXUPS = [
    # GCL_GetOption also moves the script read pointer (next_str_ptr)
    (r"GM_ConfigControlString\(\s*(\w+)\s*,\s*GCL_GetOption\(\s*'p'\s*\)\s*,\s*GCL_GetOption\(\s*'d'\s*\)\s*\)",
     r"({ char *gcl_p_ = GCL_GetOption('p'); char *gcl_d_ = GCL_GetOption('d'); "
     r"GM_ConfigControlString(\1, gcl_p_, gcl_d_); })"),
]

FIXUPS = [
    # script arguments read in order (see GLOBAL_FIXUPS)
    ('game/script.c', r'MENU_RadioCall\(GCL_GetNextInt\(\),[^;]*?GCL_GetNextInt\(\),[^;]*?GCL_GetNextInt\(\)\);[^\n]*',
     '({ int gcl_a_ = GCL_GetNextInt(); int gcl_b_ = GCL_GetNextInt(); int gcl_c_ = GCL_GetNextInt(); '
     'MENU_RadioCall(gcl_a_, gcl_b_, gcl_c_); });'),
    ('game/script.c', r'MENU_SetRadioMesg\(GCL_GetNextInt\(\),[^;]*?GCL_GetString\(GCL_NextStr\(\)\)\);[^\n]*',
     '({ int gcl_a_ = GCL_GetNextInt(); char *gcl_s_ = GCL_GetString(GCL_NextStr()); '
     'MENU_SetRadioMesg(gcl_a_, gcl_s_); });'),
    ('game/alert.c', r'GM_SeSet2\(GCL_GetNextInt\(\),[^;]*?GCL_GetNextInt\(\),[^;]*?GCL_GetNextInt\(\)\);[^\n]*',
     '({ int gcl_a_ = GCL_GetNextInt(); int gcl_b_ = GCL_GetNextInt(); int gcl_c_ = GCL_GetNextInt(); '
     'GM_SeSet2(gcl_a_, gcl_b_, gcl_c_); });'),
    # PS1 section placement (.sbss/.bss) has no meaning natively and clashes
    # with initialized definitions on ELF/PE.
    ('include/mgstype.h', r'#define SECTION\(x\)\s+__attribute__\(\(section\(x\)\)\)', '#define SECTION(x)'),
    # GCC 2.x allowed pasting '(' with a variadic argument
    ('mts/mts_new.h', r'printf\( ##args \)', 'printf( args )'),
    # casts as lvalues were removed after GCC 3.x
    ('menu/radioanim.c', r'\(\(int\)\(offset\) = \(int\)\(ptr\) \+ \(int\)\(offset\)\)',
     '(*(int *)&(offset) = (int)(ptr) + (int)(offset))'),
    # the directory scan prints found->pos also for files missing from the
    # table (found == NULL); the console just reads address 0
    ('libfs/srchfile.c', r'size, found->pos\);', 'size, found ? found->pos : 0);'),
    # memcard_check() returns without a value after memcard_retry(); on the
    # console v0 then holds memcard_dummy()'s result (memcard_retry ends with a
    # tail call), e.g. 0xC0000003 for an empty slot. Natively it was garbage,
    # so the title screen took an empty slot 2 for a good card.
    ('memcard/memcard.c', r'static void memcard_retry\(int port\);', 'static int memcard_retry(int port);'),
    ('memcard/memcard.c', r'static void memcard_retry\(int port\)\n\{', 'static int memcard_retry(int port)\n{'),
    ('memcard/memcard.c', r'memcard_dummy\(op\);\n(\s*)return;', r'return memcard_dummy(op);'),
    ('memcard/memcard.c', r'(\n\s*)memcard_dummy\(op\);\n\}', r'\1return memcard_dummy(op);\n}'),
    ('memcard/memcard.c', r'memcard_retry\(port\);\n(\s*)return;', r'return memcard_retry(port);'),
    ('memcard/memcard.c', r'return; // FIXME: Return without value', r'return sw_card_op; // FIXME: Return without value'),
    # reads the vector at address 0 (start of RAM on the console)
    ('takabe/dummy_fl.c', r'\*\(SVECTOR \*\)0x00000000', '*(SVECTOR *)0x80000000'),
    # --watch-stack: watch the actor loop's return address (debugging aid)
    ('libgv/actor.c', r'void GV_ExecActorSystem\( void \)\n\{\n(\s*)AList \*list;\n(\s*)int    i, pause;\n',
     r'void GV_ExecActorSystem( void )\n{\n\1AList *list;\n\2int    i, pause;\n\n'
     r'\1if ( psx_watch_stack )\n\1    psx_watch_return_slot( (char *)__builtin_frame_address( 0 ) + 4 );\n'),
    # 24-bit links taken as pointers: valid KUSEG addresses on the console,
    # natively they have to be resolved like the GPU does (mgs_ptr24)
    ('libdg/sort.c', r'list = \(unsigned int \*\)\(index & 0xffffff\);',
     'list = (unsigned int *)mgs_ptr24(index & 0xffffff);'),
    # ...and the list heads in DG_DivideBuffer, which divide.c stores masked
    ('libdg/sort.c', r'list = \*buf\+\+;', 'list = (unsigned int *)mgs_ptr24((unsigned long)*buf++);'),
    ('takabe/libdg2.c', r'tag = \*buf\+\+;', 'tag = (unsigned int *)mgs_ptr24((unsigned long)*buf++);'),
    # a pointer cut to 24 bits with getaddr() and used as a pointer again
    ('libdg/divide.c', r'pack = \(POLY_GT4\*\)getaddr\( &org_pack \);',
     'pack = (POLY_GT4*)mgs_ptr24(getaddr( &org_pack ));'),
    # ...and stored back into the ordering table as 24-bit links again, as the
    # original did: a full pointer's top byte would land in the next packet's
    # length (0x80 | 0x0C = 140 words: garbage executed as GPU commands)
    ('libdg/sort.c', r'\*ot_ptr = \(unsigned int\)list;', '*ot_ptr = (unsigned int)list & 0xffffff;'),
    ('takabe/libdg2.c', r'\*next = \(unsigned int\)tag;', '*next = (unsigned int)tag & 0xFFFFFF;'),
    ('takabe/libdg2.c', r'tag = \(unsigned int \*\)\(val & 0xFFFFFF\);',
     'tag = (unsigned int *)mgs_ptr24(val & 0xFFFFFF);'),
    ('include/libgpu.h', r'#define nextPrim\(p\)\s+\(void \*\)\(\(\(\(P_TAG \*\)\(p\)\)->addr\)\|0x80000000\)',
     '#define nextPrim(p)  \t\tmgs_ptr24(((P_TAG *)(p))->addr)'),
    # diagnostics: the first records the game takes from the CD stream
    ('libfs/stream.c', r'\*ptr = type \| 0x80;\n(\s*)return ptr \+ 4;',
     r'*ptr = type | 0x80;\n\1mgs_trace_stream(type, size, ptr);\n\1return ptr + 4;'),
    # the kernel's thread tables live at physical address 0x100 on the console
    ('mts/mts_new.c', r'\(struct ToT \*\)0x100', '((struct ToT *)psx_ToT)'),
    # keep the host printf: MTS replaces it with a dummy in release builds
    ('mts/mts_new.c', r'^int printf\(\)\n\{', 'int mgs_dummy_printf()\n{'),
    # the idle task busy-waits for interrupts; yield to the host instead
    ('mts/mts_new.c', r'while \( !mts_sio_unlocked \)\n(\s*);', r'while ( !mts_sio_unlocked )\n\1psx_idle();'),
    ('mts/mts_new.c', r'if \( ch < 0 \)\n(\s*)\{\n(\s*)continue;', r'if ( ch < 0 )\n\1{\n\2psx_idle();\n\2continue;'),
    ('takabe/thing.c', r'\(int\)find \|= 0x80000000;', '*(int *)&find |= 0x80000000;'),
    # the game patches digits into this string literal at runtime; literals
    # are read-only on the host, so give the strings writable storage
    ('enemy/action.c',
     r'const char \*s00a_off_800C33F4\[3\] = \{\n    "RATION",\n    "SOCOM/BULLET \* 12",\n    "FA-MAS/BULLET \* 25"\n\};',
     'static char s00a_str0[] = "RATION";\nstatic char s00a_str1[] = "SOCOM/BULLET * 12";\n'
     'static char s00a_str2[] = "FA-MAS/BULLET * 25";\n'
     'char *s00a_off_800C33F4[3] = { s00a_str0, s00a_str1, s00a_str2 };'),
    # the title screen overlay is not decompiled yet: like upstream's dev
    # variant, start in the debug stage select instead (until it is)
    ('game/loader.c', r'#ifdef DEV_EXE\n(\s*)// force load some overlay in dev variant\n(\s*)if \(strcmp\(dir, "title"\) == 0\)',
     r'#if defined(DEV_EXE) || defined(MGS_NATIVE)\n\1// force load some overlay in dev variant\n\2if (strcmp(dir, "title") == 0 && !mgs_native_overlay_available("title"))'),
    # swap in the native chara table once a stage binary was loaded
    # stage binaries in the streamed region ('s') are read straight to the
    # overlay address, without GM_LoadInitBin: note their id and run the
    # overlay hook once the whole stage is loaded (before its scenario runs)
    ('libfs/cdstage.c', r'^(\s*)task->buffer = StageCharacterEntries;',
     r'\1task->buffer = StageCharacterEntries;\n\1mgs_stage_bin_id = tag->id;'),
    ('game/loader.c', r'FS_LoadStageComplete\(work->info\);',
     'FS_LoadStageComplete(work->info);\n    mgs_native_stage_loaded();'),
    ('game/gamed.c', r'memcpy\(StageCharacterEntries, buf, gOverlayBinSize_800B5290\);',
     'memcpy(StageCharacterEntries, buf, gOverlayBinSize_800B5290);\n    mgs_native_overlay_loaded(id);'),
]

FILE_SCOPE_EXTERN = re.compile(r'^extern\s+[^;()]*?\b(\w+)\s*(?:\[[^\]]*\])?\s*;', re.M)


def drop_static_after_extern(text):
    """`extern int x; ... static int x;` was accepted by GCC 2.x; modern GCC
    rejects it. Make the later definition non-static (same object)."""
    names = set(FILE_SCOPE_EXTERN.findall(text))
    if not names:
        return text

    def repl(m):
        return m.group(0)[len('static'):].lstrip() if m.group(1) in names else m.group(0)
    return re.sub(r'^static[ \t]+[^;={\n]*?\b(\w+)\s*(?:\[[^\]]*\])?\s*(?=[;=\[])', repl, text, flags=re.M)

KEYWORD = re.compile(r'\b(__asm__|asm)\b(\s*(?:volatile|__volatile__))?\s*\(')


class AsmError(Exception):
    pass


def find_close(text, i):
    """i points just after '('; return index of the matching ')'."""
    depth = 1
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while text[i] != '"':
                i += 2 if text[i] == '\\' else 1
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise AsmError('unterminated asm')


def split_top(s, sep):
    parts, depth, cur, i = [], 0, '', 0
    while i < len(s):
        c = s[i]
        if c == '"':
            j = i + 1
            while s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            cur += s[i:j + 1]
            i = j + 1
            continue
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
        if c == sep and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += c
        i += 1
    parts.append(cur)
    return parts


def parse_operands(s):
    ops = []
    for part in split_top(s, ','):
        part = part.strip()
        if not part:
            continue
        m = re.match(r'^((?:"[^"]*"\s*)+)\((.*)\)$', part, re.S)
        if not m:
            raise AsmError('bad operand: ' + part)
        ops.append((''.join(re.findall(r'"([^"]*)"', m.group(1))), m.group(2).strip()))
    return ops


class Gen:
    def __init__(self, outputs, inputs):
        self.ops = outputs + inputs
        self.n_out = len(outputs)
        self.regs = set()
        self.body = []
        # register -> address expression it was last loaded from (lw from an
        # operand base), used to tell the 60 fps interpolation which matrix
        # the GTE's rotation registers came from
        self.loaded_from = {}

    def reg(self, tok):
        tok = tok.strip()
        if tok.startswith('%'):
            return '_o%d' % int(tok[1:])
        if tok.startswith('$'):
            name = tok[1:]
            n = int(name) if name.isdigit() else REG_NAMES[name]
            if n == 0:
                return '0u'
            self.regs.add(n)
            return '_r%d' % n
        raise AsmError('bad register ' + tok)

    def is_sp(self, tok):
        tok = tok.strip()
        return tok in ('$29', '$sp')

    def mem(self, tok):
        m = re.match(r'^\s*(-?(?:0x[0-9a-fA-F]+|\d+))?\s*\(\s*([^)]+?)\s*\)\s*$', tok)
        if not m:
            raise AsmError('bad mem operand ' + tok)
        off = int(m.group(1), 0) if m.group(1) else 0
        return '(%s + (%d))' % (self.reg(m.group(2)), off)

    def cop2(self, n):
        return int(n.strip().lstrip('$'))

    def emit(self, ins):
        ins = ins.strip()
        if not ins:
            return
        parts = ins.split(None, 1)
        op = parts[0]
        args = [a.strip() for a in split_top(parts[1], ',')] if len(parts) > 1 else []
        b = self.body
        if op == 'nop':
            return
        if op == 'break':
            return
        # stack pointer juggling (SetSpadStack & co.) is meaningless natively
        if op in ('move', 'addiu', 'addi', 'addu', 'subu', 'lw', 'or') and args and self.is_sp(args[0]):
            return
        # ...including saving the old stack pointer (sw $sp, 0($t0)), which
        # natively would store to an untranslated, i.e. null, address
        if op == 'sw' and args and self.is_sp(args[0]):
            return
        if op not in ('lw', 'lh', 'lhu', 'lb', 'lbu', 'ctc2', 'mtc2', 'lwc2', 'swc2', 'sw', 'sh', 'sb') \
                and args and not self.is_sp(args[0]) and args[0].startswith(('$', '%')):
            self.loaded_from.pop(self.reg(args[0]), None)
        if op == 'cop2':
            b.append('GTE_OP(%s);' % args[0])
        elif op == '.word':
            w = int(args[0], 0)
            if (w >> 25) != 0x25:
                raise AsmError('unsupported .word %s' % args[0])
            b.append('GTE_OP(0x%x);' % (w & 0x1ffffff))
        elif op == 'lwc2':
            b.append('GTE_WD(%d, *(unsigned int *)%s);' % (self.cop2(args[0]), self.mem(args[1])))
        elif op == 'swc2':
            b.append('*(unsigned int *)%s = GTE_RD(%d);' % (self.mem(args[1]), self.cop2(args[0])))
        elif op == 'mtc2':
            b.append('GTE_WD(%d, %s);' % (self.cop2(args[1]), self.reg(args[0])))
        elif op == 'mfc2':
            b.append('%s = GTE_RD(%d);' % (self.reg(args[0]), self.cop2(args[1])))
        elif op == 'ctc2':
            b.append('GTE_WC(%d, %s);' % (self.cop2(args[1]), self.reg(args[0])))
            if self.cop2(args[1]) == 0 and self.reg(args[0]) in self.loaded_from:
                b.append('GTE_NOTE_RT(%s);' % self.loaded_from[self.reg(args[0])])
        elif op == 'cfc2':
            b.append('%s = GTE_RC(%d);' % (self.reg(args[0]), self.cop2(args[1])))
        elif op in ('lw', 'lh', 'lhu', 'lb', 'lbu'):
            t = {'lw': 'unsigned int', 'lh': 'short', 'lhu': 'unsigned short',
                 'lb': 'signed char', 'lbu': 'unsigned char'}[op]
            addr = self.mem(args[1])
            b.append('%s = (unsigned int)(int)*(%s *)%s;' % (self.reg(args[0]), t, addr))
            if op == 'lw' and '_o' in addr:
                self.loaded_from[self.reg(args[0])] = addr
            else:
                self.loaded_from.pop(self.reg(args[0]), None)
        elif op in ('sw', 'sh', 'sb'):
            t = {'sw': 'unsigned int', 'sh': 'unsigned short', 'sb': 'unsigned char'}[op]
            src = '0u' if self.is_sp(args[0]) else self.reg(args[0])
            b.append('*(%s *)%s = (%s)%s;' % (t, self.mem(args[1]), t, src))
        elif op == 'move':
            b.append('%s = %s;' % (self.reg(args[0]), self.reg(args[1])))
        elif op in ('sll', 'srl'):
            o = '<<' if op == 'sll' else '>>'
            b.append('%s = %s %s %d;' % (self.reg(args[0]), self.reg(args[1]), o, int(args[2], 0)))
        elif op == 'sra':
            b.append('%s = (unsigned int)((int)%s >> %d);' % (self.reg(args[0]), self.reg(args[1]), int(args[2], 0)))
        elif op in ('addu', 'subu', 'or', 'and'):
            o = {'addu': '+', 'subu': '-', 'or': '|', 'and': '&'}[op]
            b.append('%s = %s %s %s;' % (self.reg(args[0]), self.reg(args[1]), o, self.reg(args[2])))
        elif op in ('addiu', 'addi'):
            b.append('%s = %s + (unsigned int)(%d);' % (self.reg(args[0]), self.reg(args[1]), int(args[2], 0)))
        else:
            raise AsmError('unsupported instruction: ' + ins)

    def code(self):
        decl = []
        for i, (cons, expr) in enumerate(self.ops):
            if i < self.n_out:
                if '+' in cons:
                    decl.append('unsigned int _o%d = (unsigned int)(%s);' % (i, expr))
                else:
                    decl.append('unsigned int _o%d = 0;' % i)
            else:
                decl.append('unsigned int _o%d = (unsigned int)(%s);' % (i, expr))
        for r in sorted(self.regs):
            decl.append('unsigned int _r%d = 0;' % r)
        tail = []
        for i in range(self.n_out):
            tail.append('(%s) = _o%d;' % (self.ops[i][1], i))
        unused = ['(void)_o%d;' % i for i in range(len(self.ops))] + ['(void)_r%d;' % r for r in sorted(self.regs)]
        return 'do { ' + ' '.join(decl + self.body + tail + unused) + ' } while (0)'


def translate_asm(inner):
    inner = re.sub(r'\\\r?\n', ' ', inner)
    parts = split_top(inner, ':')
    template = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', parts[0]))
    outputs = parse_operands(parts[1]) if len(parts) > 1 else []
    inputs = parse_operands(parts[2]) if len(parts) > 2 else []
    instrs = [i for i in re.split(r';|\\n', template) if i.strip()]
    if not instrs:
        return None  # scheduling barrier / register binding
    if len(parts) == 1 and len(instrs) == 1 and re.fullmatch(r'\s*\$?\w+\s*', instrs[0]) \
            and instrs[0].strip().lstrip('$') in list(REG_NAMES) + [str(i) for i in range(32)]:
        return None  # register int x asm("v1")
    g = Gen(outputs, inputs)
    for ins in instrs:
        g.emit(ins)
    return g.code()


def mask_comments(text):
    """Return a copy with comments blanked so the keyword scan ignores them."""
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"' and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            i = j + 1
        elif text[i] == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            i = j + 1
        else:
            i += 1
    return ''.join(out)


def translate(text, path):
    masked = mask_comments(text)
    out = []
    pos = 0
    for m in KEYWORD.finditer(masked):
        if m.start() < pos:
            continue
        open_end = m.end()
        close = find_close(text, open_end)
        try:
            repl = translate_asm(text[open_end:close])
        except AsmError as e:
            raise AsmError('%s: %s' % (path, e))
        out.append(text[pos:m.start()])
        if repl is None:
            # keep the line structure intact inside macros
            out.append(' ' * 0)
        else:
            out.append(repl)
        pos = close + 1
    out.append(text[pos:])
    return ''.join(out)


def write_if_changed(path, data):
    try:
        with open(path, 'rb') as f:
            if f.read() == data:
                return False
    except FileNotFoundError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    return True


def main():
    out_dir = sys.argv[1]
    changed = 0
    for spec in sys.argv[2:]:
        lenient = spec.startswith('?')
        src_root, dst_sub = spec.lstrip('?').split(':', 1)
        for dp, _, files in os.walk(src_root):
            for fn in files:
                src = os.path.join(dp, fn)
                rel = os.path.relpath(src, src_root)
                with open(src, 'rb') as f:
                    raw = f.read()
                if not fn.endswith(('.c', '.h')):
                    # included data files (e.g. libfs/file.cnf) are copied as is
                    if write_if_changed(os.path.join(out_dir, dst_sub, rel), raw):
                        changed += 1
                    continue
                text = raw.decode('utf-8', errors='surrogateescape')
                if KEYWORD.search(text):
                    try:
                        text = translate(text, src)
                    except AsmError:
                        if not lenient:
                            raise
                if not lenient:
                    text = drop_static_after_extern(text)
                for pat, rep in GLOBAL_FIXUPS:
                    text = re.sub(pat, rep, text, flags=re.M)
                for suffix, pat, rep in FIXUPS:
                    if src.replace('\\', '/').endswith(suffix):
                        text, n = re.subn(pat, rep, text, flags=re.M)
                        if n == 0:
                            raise AsmError('fixup no longer applies: %s' % suffix)
                data = text.encode('utf-8', errors='surrogateescape')
                if write_if_changed(os.path.join(out_dir, dst_sub, rel), data):
                    changed += 1
    print('asm2c: %d file(s) updated' % changed)


if __name__ == '__main__':
    try:
        main()
    except AsmError as e:
        sys.exit('asm2c: error: %s' % e)
