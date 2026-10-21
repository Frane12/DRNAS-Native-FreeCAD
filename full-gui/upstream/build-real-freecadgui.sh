#!/usr/bin/env bash
# Compile the ACTUAL pinned upstream src/Gui/FreeCADGui Android ARM64 target.
# Keep original successful headless FreeCADBase/App build script unchanged.
# Failure is an honest GUI dependency blocker; do not quietly substitute
# our working OCCT Android viewport for upstream FreeCADGui.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/upstream-freecad-gui"
mkdir -p "$OUT"
base="$ROOT/full-port/build-full-core.sh"
driver="$OUT/genuine-FreeCADGui-build-driver.sh"
python3 - "$base" "$driver" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
assert s.count('-DBUILD_GUI=OFF \\') == 1, "Core script's GUI switch changed unexpectedly"
s=s.replace('-DBUILD_GUI=OFF \\', '-DBUILD_GUI=ON \\')
# Keep first configure as complete FreeCAD with real GUI and intended CAD modules.
# No stubs for missing COIN/OpenGL/GLU/PySide or other prerequisites.
s=s.replace('GUI=OFF (first full native core target FreeCADBase+FreeCADApp)',
            'GUI=ON (actual upstream FreeCADGui target; do not fake Coin3D)')
assert s.count('--target FreeCADBase FreeCADApp') == 1
s=s.replace('--target FreeCADBase FreeCADApp',
            '--target FreeCADBase FreeCADApp FreeCADGui')
# Attempt genuine native FreeCADGui and fail if its .so is absent.
needle='echo \\'FULL FreeCADBase + FreeCADApp Android ARM64: BUILD PASS\\' | tee "$OUT/full-core-result.txt"'
assert s.count(needle)==1
s=s.replace(needle, """
gui="$(find "$BUILDDIR" -type f -name 'libFreeCADGui.so' -print -quit)"
test -n "$gui" || { echo 'REAL upstream FreeCADGui library missing' >&2; exit 1; }
readelf -h "$gui" | grep -q 'Machine:.*AArch64'
readelf -h "$gui" | grep -q 'Type:.*DYN'
! readelf -d "$gui" | grep -Eq 'libc\\\\.so\\\\.6|ld-linux'
sha256sum "$gui" | tee -a "$OUT/full-gui-sha256.txt"
echo 'UPSTREAM FreeCADGui Android ARM64 compiled: BUILD PASS' | tee "$OUT/full-gui-result.txt"
""")
# Preserve and tag the GUI CMake/configure/build logs under own output dir.
s=s.replace('OUT="$ROOT/out/full-freecad"', 'OUT="$ROOT/out/upstream-freecad-gui"')
s=s.replace('BUILDDIR="$ROOT/.build/freecad-upstream-android-arm64"',
            'BUILDDIR="$ROOT/.build/freecad-upstream-real-gui-arm64"')
Path(sys.argv[2]).write_text(s)
PY
# Uses pinned FreeCAD source, verified Android Qt/OCCT/ICU/Boost/Python/Xerces
# and native NDK toolchain (same exact versions as proven core build).
bash "$driver"
