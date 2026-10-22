#!/usr/bin/env python3
"""
Prelink one stage overlay into a single relocatable object for the native
executable.

On the console every overlay is linked on its own against the main
executable, so different overlays may define the same symbols. Natively all
overlays live in one executable; to keep them apart, every symbol an overlay
defines is made local, except its chara table, which is renamed to
mgs_ov_<name>. References to the main executable stay undefined and are
resolved by the final link. Common (tentative) symbols stay global so they
keep resolving to the main executable's variables, as psylink does.

Usage: link_overlay.py <cc> <nm> <objcopy> <symbol_prefix> <name> <out.o> <obj>...
"""
import os
import subprocess
import sys


def run(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        sys.exit('link_overlay: failed: ' + ' '.join(cmd[:4]) + ' ...')
    return r.stdout


def main():
    cc, nm, objcopy, prefix, name, out = sys.argv[1:7]
    objs = sys.argv[7:]
    tmp = out + '.tmp.o'

    cc_args = cc.split(';')
    run(cc_args + ['-r', '-nostdlib', '-o', tmp] + objs)

    table = prefix + '_StageCharacterEntries'
    localize = []
    for line in run([nm, '-g', '--defined-only', tmp]).splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        kind, sym = parts[1], parts[2]
        if kind in ('C', 'c') or sym == table:
            continue
        localize.append(sym)

    syms = out + '.localize'
    with open(syms, 'w') as f:
        f.write('\n'.join(localize) + '\n')
    run([objcopy, '--localize-symbols=' + syms,
         '--redefine-sym', '%s=%smgs_ov_%s' % (table, prefix, name), tmp, out])
    os.remove(tmp)


if __name__ == '__main__':
    main()
