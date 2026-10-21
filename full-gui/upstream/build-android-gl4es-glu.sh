#!/usr/bin/env bash
# REAL GL4ES + real Mesa GLU, cross-compiled against Android/Bionic.
# Both sources are pinned. This is an OpenGL compatibility investigation,
# not a claim that FreeCADGui already renders.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/out/upstream-freecad-gui/opengl-compat"
SRC="$ROOT/.deps"
BUILD="$ROOT/.build/freecad-gl4es-android"
NDK_VERSION="27.2.12479018"
ANDROID_API="26"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
PIN_GL4ES="a025615bc79ac43ec911dced8fe5b8eb45a0b72d"
PIN_GLU="2fed2bda2b725d2b9e32c435b48d5141cc95827f"
mkdir -p "$OUT/lib" "$OUT/include" "$SRC" "$BUILD"
fetch() {
  local repo="$1" sha="$2" target="$3"
  if [[ ! -d "$target/.git" ]]; then
    git init -q "$target"
    git -C "$target" remote add origin "https://github.com/$repo.git"
  fi
  git -C "$target" fetch --depth 1 origin "$sha"
  git -C "$target" checkout --detach --force FETCH_HEAD
  [[ "$(git -C "$target" rev-parse HEAD)" == "$sha" ]]
}
fetch ptitSeb/gl4es "$PIN_GL4ES" "$SRC/gl4es-freecad"
fetch ptitSeb/GLU "$PIN_GLU" "$SRC/glu-freecad"
cmake -S "$SRC/gl4es-freecad" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$ANDROID_API" \
  -DCMAKE_BUILD_TYPE=Release -DANDROID=ON -DNOX11=ON -DNOEGL=ON \
  -DDEFAULT_ES=2 -DUSE_ANDROID_LOG=ON \
  2>&1 | tee "$OUT/gl4es-configure.log"
cmake --build "$BUILD" --parallel 4 2>&1 | tee "$OUT/gl4es-compile.log"
gl="$(find "$SRC/gl4es-freecad/lib" -maxdepth 1 -name 'libGL.so*' -type f -print -quit)"
test -f "$gl" || { echo 'Upstream GL4ES libGL.so not built' >&2; exit 1; }
cp -L "$gl" "$OUT/lib/libGL.so"
test -f "$SRC/gl4es-freecad/include/GL/gl.h"
cp -a "$SRC/gl4es-freecad/include/." "$OUT/include/"

# Standard GLU triangulation/NURBS by upstream ptitSeb/Mesa GLU source.
"$NDK/ndk-build" -C "$SRC/glu-freecad" \
  NDK_PROJECT_PATH="$SRC/glu-freecad" \
  APP_BUILD_SCRIPT="$SRC/glu-freecad/Android.mk" \
  APP_ABI=arm64-v8a APP_PLATFORM="android-$ANDROID_API" APP_STL=c++_shared \
  APP_CPPFLAGS="-std=gnu++14" \
  NDK_OUT="$BUILD/glu-obj" NDK_LIBS_OUT="$BUILD/glu-libs" \
  -j4 2>&1 | tee "$OUT/glu-compile.log"
gluStatic="$(find "$BUILD/glu-obj" -name 'libGLU.a' -type f -print -quit)"
test -f "$gluStatic"
test -f "$SRC/glu-freecad/include/GL/glu.h"
cp -a "$SRC/glu-freecad/include/." "$OUT/include/"
CC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$ANDROID_API-clang++"
"$CC" -shared -Wl,-soname,libGLU.so -Wl,--no-undefined \
  -Wl,--whole-archive "$gluStatic" -Wl,--no-whole-archive \
  -L"$OUT/lib" -lGL -o "$OUT/lib/libGLU.so" \
  2>&1 | tee "$OUT/glu-link.log"
for so in "$OUT/lib/libGL.so" "$OUT/lib/libGLU.so"; do
  readelf -h "$so" | grep -q 'Machine:.*AArch64'
  readelf -h "$so" | grep -q 'Type:.*DYN'
  if readelf -d "$so" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'; then
    echo "Host Linux library not permitted: $so" >&2; exit 1
  fi
  readelf -d "$so" >> "$OUT/elf-deps.txt"
  sha256sum "$so" >> "$OUT/SHA256SUMS"
done
echo "PASS: upstream GL4ES + real Mesa GLU Android ARM64 compatibility layer linked" > "$OUT/STATUS.txt"
cat "$OUT/STATUS.txt"
