#!/usr/bin/env bash
# Richtet die Build-Umgebung fuer den MGS-Decomp ein (Ubuntu/Debian, auch WSL).
# - holt die Submodule (mgs_reversing + psyq_sdk)
# - installiert Wine inkl. 32-Bit-Unterstuetzung (fuer die PsyQ-Compiler)
# - legt ein Python-venv mit den Build-Abhaengigkeiten an
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV="${VENV:-$ROOT/.venv}"

echo "==> Submodule initialisieren"
git -C "$ROOT" submodule update --init --recursive

if ! command -v wine >/dev/null 2>&1; then
    echo "==> Wine installieren (benoetigt sudo)"
    SUDO=""
    [ "$(id -u)" -ne 0 ] && SUDO="sudo"
    $SUDO dpkg --add-architecture i386
    $SUDO apt-get update
    DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y wine wine32:i386 ninja-build python3-venv
fi

echo "==> Python-venv in $VENV"
python3 -m venv "$VENV"
"$VENV/bin/pip" install -q -r "$ROOT/upstream/mgs_reversing/build/requirements.txt"

echo "==> Wine-Prefix initialisieren"
WINEDEBUG=-all wineboot -i >/dev/null 2>&1 || true

echo "Fertig. Build starten mit: scripts/build.sh"
