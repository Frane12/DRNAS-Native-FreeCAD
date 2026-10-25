#!/usr/bin/env bash
# Host-only regression. No desktop library is copied into the Android payload.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_SRC="$ROOT/.build/render-isolation/gl4es"
TEST_BUILD="$ROOT/.build/render-isolation/gl4es-build"
mkdir -p "$TEST_SRC"
git -C "$TEST_SRC" init -q
git -C "$TEST_SRC" fetch --depth 1 https://github.com/ptitSeb/gl4es.git a025615bc79ac43ec911dced8fe5b8eb45a0b72d
git -C "$TEST_SRC" checkout --detach --force FETCH_HEAD
python3 "$ROOT/full-gui/upstream/patch-qt-gl-state.py" "$TEST_SRC/src/gl/framebuffers.c"
cmake -S "$TEST_SRC" -B "$TEST_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DNOX11=ON -DNOEGL=ON -DNO_GBM=ON \
  -DDEFAULT_ES=2 -DCMAKE_C_FLAGS=-DDRNAS_QT_BRIDGE_TEST
cmake --build "$TEST_BUILD" --parallel 4
cmake -S "$ROOT/full-gui/qt-activity/tests" -B "$ROOT/.build/render-isolation/tests" -G Ninja
cmake --build "$ROOT/.build/render-isolation/tests" --parallel 4
ctest --test-dir "$ROOT/.build/render-isolation/tests" --output-on-failure
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 LIBGL_GLES=/usr/lib/x86_64-linux-gnu/libGLESv2.so.2 \
  "$ROOT/.build/render-isolation/tests/gl-state-isolation" "$TEST_SRC/lib/libGL.so.1"
