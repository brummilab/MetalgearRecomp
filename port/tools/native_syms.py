#!/usr/bin/env python3
"""
Symbol lists for the static recompiler (tools/recomp.py).

Usage: native_syms.py <nm> --prefix=<prefix> <out-native> <out-runtime> <runtime-lib> <object>...

<out-native> gets every symbol the native program has: the symbols defined
by the game objects and the runtime, plus the ones they reference (the C
library). <out-runtime> gets the functions the PlayStation runtime defines,
which replace statically linked PsyQ library code in overlays. <prefix> is
the C symbol prefix of the target ("_" on 32-bit Windows).
"""
import subprocess
import sys


def nm(tool, paths):
    out = subprocess.run([tool, '-P'] + paths, check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and not parts[0].endswith(':') and not parts[0].endswith(']'):
            yield parts[0], parts[1]


def strip(name, prefix):
    return name[len(prefix):] if prefix and name.startswith(prefix) else name


def write(path, names):
    data = '\n'.join(sorted(names)) + '\n'
    try:
        with open(path) as f:
            if f.read() == data:
                return
    except FileNotFoundError:
        pass
    with open(path, 'w') as f:
        f.write(data)


def main():
    tool, prefix, out_native, out_runtime, lib = sys.argv[1:6]
    prefix = prefix[len('--prefix='):]
    objs = sys.argv[6:]
    native, runtime = set(), set()
    for name, kind in nm(tool, objs + [lib]):
        if kind in 'TDBRCVWU' and not name.startswith('.'):
            native.add(strip(name, prefix))
    for name, kind in nm(tool, [lib]):
        if kind == 'T':
            runtime.add(strip(name, prefix))
    write(out_native, native)
    write(out_runtime, runtime)


if __name__ == '__main__':
    main()
