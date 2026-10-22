#!/usr/bin/env bash
# Richtet die Build-Umgebung ein (Ubuntu/Debian, auch WSL):
# - Submodule (mgs_reversing + psyq_sdk)
# - Wine mit 32-Bit-Unterstützung (PsyQ-Compiler für den PS1-Build)
# - 32-Bit-GCC, CMake, Ninja, SDL2 (i386) für den nativen PC-Port
# - optional MinGW für den Windows-Cross-Build (WITH_MINGW=1)
# - Python-venv mit den Build-Abhängigkeiten
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV="${VENV:-$ROOT/.venv}"
SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"

echo "==> Submodule initialisieren"
git -C "$ROOT" submodule update --init --recursive

echo "==> Pakete installieren (benoetigt sudo)"
PKGS="wine wine32:i386 ninja-build cmake python3-venv gcc-multilib libsdl2-dev:i386"
[ "${WITH_MINGW:-0}" = "1" ] && PKGS="$PKGS gcc-mingw-w64-i686"
$SUDO dpkg --add-architecture i386
$SUDO apt-get update
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y $PKGS

echo "==> Python-venv in $VENV"
python3 -m venv "$VENV"
"$VENV/bin/pip" install -q -r "$ROOT/upstream/mgs_reversing/build/requirements.txt"

echo "==> Wine-Prefix initialisieren"
WINEDEBUG=-all wineboot -i >/dev/null 2>&1 || true

echo "Fertig."
echo "  PS1-Build (matching):  scripts/build.sh"
echo "  PC-Port (Linux):       scripts/build-port.sh"
echo "  PC-Port (Windows):     WITH_MINGW=1 scripts/setup.sh && scripts/build-port.sh windows"
