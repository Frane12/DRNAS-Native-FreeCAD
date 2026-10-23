#!/usr/bin/env bash
# Fail-closed Android binding source/toolchain preflight; does not fabricate wheels.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/python-bindings-build"
mkdir -p "$OUT"
exec > >(tee "$OUT/preparation.log") 2>&1
QT="$ROOT/out/full-deps/qt/6.8.3/android_arm64_v8a"
PY="$ROOT/out/full-deps/python"
NDK="$ANDROID_HOME/ndk/${NDK_VERSION:-27.2.12479018}"
for path in "$QT/lib/cmake/Qt6/Qt6Config.cmake" "$PY/include/python3.14/Python.h" "$NDK/build/cmake/android.toolchain.cmake"; do
  test -s "$path" || { echo "::error::Required Android cross-compile SDK missing: $path"; exit 1; }
done
mkdir -p "$ROOT/.deps"
if [[ ! -d "$ROOT/.deps/pyside-setup/.git" ]]; then
  git clone --depth 1 --branch v6.8.3 https://code.qt.io/pyside/pyside-setup.git "$ROOT/.deps/pyside-setup"
fi
if [[ ! -d "$ROOT/.deps/pivy/.git" ]]; then
  git clone --depth 1 https://github.com/coin3d/pivy.git "$ROOT/.deps/pivy"
fi
git -C "$ROOT/.deps/pyside-setup" rev-parse HEAD > "$OUT/pyside-source-sha.txt"
git -C "$ROOT/.deps/pivy" rev-parse HEAD > "$OUT/pivy-source-sha.txt"
cat > "$OUT/STATUS.txt" <<'TXT'
SOURCES PREPARED, NOT COMPILED.
Shiboken6/PySide6 need host shiboken generator, Qt 6.8.3 target SDK and
a verified CPython 3.14 Android cross-build.
Pivy requires real Coin3D development headers/library from the FreeCAD build
plus SWIG configured for the same Android CPython ABI.
Do not publish a wheel or APK until those build steps and device import tests pass.
TXT
echo "::error::ARM64 Python bindings cross-compilation is NOT implemented yet. Source checkout is not a compiled artifact."
exit 1
