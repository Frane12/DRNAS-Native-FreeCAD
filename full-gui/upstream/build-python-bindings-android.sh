#!/usr/bin/env bash
# Real cross-compilation attempt. Never substitute host wheels or mock modules.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/android-python-bindings"
QT="$ROOT/out/full-deps/qt/6.8.3/android_arm64_v8a"
HOSTQT="$ROOT/out/full-deps/qt/6.8.3/gcc_64"
PY="$ROOT/out/full-deps/python"
NDK="$ANDROID_HOME/ndk/${NDK_VERSION:-27.2.12479018}"
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
mkdir -p "$OUT" "$ROOT/.build/android-python-bindings"
for p in "$QT/lib/cmake/Qt6/Qt6Config.cmake" "$PY/include/python3.14/Python.h" "$TOOLCHAIN"; do
  test -s "$p" || { echo "::error::Missing target SDK $p"; exit 1; }
done
export CMAKE_PREFIX_PATH="$QT"
export PATH="$HOSTQT/bin:$PATH"
cd "$ROOT/.build/android-python-bindings"
if [[ ! -d pyside-setup ]]; then
  git clone --depth 1 --branch v6.8.3 https://code.qt.io/pyside/pyside-setup.git
fi
# Build real shiboken6 first. Host generator and target module are distinct;
# do not use a host x86_64 extension as an Android target.
cmake -S pyside-setup/sources/shiboken6 -B shiboken-target -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT;$PY" \
  -DQt6_DIR="$QT/lib/cmake/Qt6" \
  -DPython_EXECUTABLE="$(command -v python3)" \
  -DPython_INCLUDE_DIR="$PY/include/python3.14" \
  -DPython_LIBRARY="$PY/lib/libpython3.14.so" \
  -DBUILD_TESTS=OFF \
  2>&1 | tee "$OUT/shiboken-configure.log"
cmake --build shiboken-target --parallel 4 2>&1 | tee "$OUT/shiboken-build.log"
echo 'REAL shiboken6 target cross-build finished; PySide6/Pivy still require their own build stages.'
