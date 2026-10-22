#!/usr/bin/env bash
# Baut den nativen PC-Port (32 Bit).
#
#   scripts/build-port.sh            -> Linux-Build in build/linux
#   scripts/build-port.sh windows    -> Windows-Build (MinGW-Cross) in build/windows
#
# Für den Windows-Cross-Build wird das SDL2-MinGW-Entwicklerpaket automatisch
# nach build/deps geladen. Unter Windows selbst: siehe port/README.md (MSYS2).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-linux}"
PY="${VENV:-$ROOT/.venv}/bin/python"
[ -x "$PY" ] || PY="$(command -v python3)"
SDL_VERSION=2.30.9

case "$TARGET" in
linux)
    cmake -S "$ROOT/port" -B "$ROOT/build/linux" -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE="$ROOT/port/cmake/linux-i686.cmake" \
          -DPython3_EXECUTABLE="$PY"
    ninja -C "$ROOT/build/linux"
    "$ROOT/build/linux/test_runtime"
    echo "Fertig: $ROOT/build/linux/mgs --iso <disc1.bin>"
    ;;
windows)
    DEPS="$ROOT/build/deps"
    SDL_DIR="$DEPS/SDL2-$SDL_VERSION/i686-w64-mingw32"
    if [ ! -d "$SDL_DIR" ]; then
        mkdir -p "$DEPS"
        curl -sSL -o "$DEPS/sdl2.tar.gz" \
            "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz"
        tar xzf "$DEPS/sdl2.tar.gz" -C "$DEPS"
    fi
    cmake -S "$ROOT/port" -B "$ROOT/build/windows" -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE="$ROOT/port/cmake/mingw-i686.cmake" \
          -DSDL2_DIR="$SDL_DIR/lib/cmake/SDL2" \
          -DPython3_EXECUTABLE="$PY"
    ninja -C "$ROOT/build/windows"
    cp "$SDL_DIR/bin/SDL2.dll" "$ROOT/build/windows/"
    echo "Fertig: $ROOT/build/windows/mgs.exe (+ SDL2.dll)"
    ;;
*)
    echo "usage: $0 [linux|windows]" >&2
    exit 1
    ;;
esac
