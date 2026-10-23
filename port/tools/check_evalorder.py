#!/usr/bin/env python3
"""
Finds calls whose arguments have order-dependent side effects (script reads
GCL_*, random numbers, ++/--) in the translated game sources.

The PS1 compiler evaluates arguments left to right; GCC on x86 may not, so
such calls behave differently natively. tools/asm2c.py rewrites the known
ones (GLOBAL_FIXUPS / FIXUPS); this check fails when new ones appear, e.g.
after an upstream update.

Usage: check_evalorder.py <translated-source-dir>   (exit 1 on findings)
"""
import os, re, sys
SIDE = re.compile(r'\b(GCL_Get\w*|GCL_Read\w*|GCL_Exec\w*|GCL_StrToInt|GV_Rand\w*|rand|psx_rand|GV_\w*Pop\w*)\s*\(|\+\+|--')
root = sys.argv[1]
hits = []
def split_args(s):
    depth = 0; cur = ''; out = []
    for ch in s:
        if ch in '([{': depth += 1
        if ch in ')]}': depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur); cur = ''
        else:
            cur += ch
    out.append(cur)
    return out
for dp, _, fs in os.walk(root):
    for f in fs:
        if not f.endswith('.c'): continue
        p = os.path.join(dp, f)
        src = open(p, encoding='latin-1').read()
        src_nc = re.sub(r'/\*.*?\*/', lambda m: ' ' * len(m.group(0)), src, flags=re.S)
        src_nc = re.sub(r'//[^\n]*', '', src_nc)
        for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', src_nc):
            name = m.group(1)
            if name in ('if', 'while', 'for', 'switch', 'return', 'sizeof'): continue
            i = m.end(); depth = 1; j = i
            while j < len(src_nc) and depth:
                if src_nc[j] == '(': depth += 1
                elif src_nc[j] == ')': depth -= 1
                j += 1
            args = split_args(src_nc[i:j-1])
            if len(args) < 2: continue
            n = sum(1 for a in args if SIDE.search(a))
            if n >= 2:
                line = src_nc.count('\n', 0, m.start()) + 1
                hits.append((os.path.relpath(p, root), line, ' '.join(src_nc[m.start():j].split())[:140]))
seen = set()
for h in hits:
    if (h[0], h[1]) in seen: continue
    seen.add((h[0], h[1]))
    print('%s:%d: %s' % h)
print('check_evalorder: %d call(s) with order-dependent arguments' % len(seen), file=sys.stderr)
sys.exit(1 if seen else 0)
