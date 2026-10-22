#!/usr/bin/env bash
# Baut den MGS-Integral-Decomp und prueft den Hash gegen das Original.
#
#   scripts/build.sh            -> SLPM_862.47 (Disc 1/2, matching)
#   scripts/build.sh vr_exe     -> SLPM_862.49 (VR-Disc, matching)
#   scripts/build.sh dev_exe    -> Dev-Build (nicht matching, frei aenderbar)
#
# JOBS=<n> steuert die Parallelitaet. Wine stuerzt unter Linux bei vielen
# gleichzeitigen Compiler-Prozessen gelegentlich ab, daher Standard = 2.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VARIANT="${1:-main_exe}"
JOBS="${JOBS:-2}"
PY="${VENV:-$ROOT/.venv}/bin/python"
[ -x "$PY" ] || PY="python3"
export WINEDEBUG="${WINEDEBUG:--all}"

cd "$ROOT/upstream/mgs_reversing/build"

# build.py erzeugt build.ninja und startet ninja ohne -j-Limit. Schlaegt das
# wegen Wine-Abstuerzen fehl, bauen wir mit begrenzter Parallelitaet weiter.
if "$PY" build.py --variant="$VARIANT"; then
    exit 0
fi

echo "==> Weiterbauen mit ninja -j$JOBS"
ninja -j"$JOBS"

if [ "$VARIANT" != "dev_exe" ]; then
    CMP="main"
    [ "$VARIANT" = "vr_exe" ] && CMP="vr"
    "$PY" compare.py --variant "$CMP"
    "$PY" post_build_checkup.py
fi
