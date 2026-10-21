#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FREECAD_REF="${FREECAD_REF:-d4ffc7f725973e073907746e88d320d3e438fd34}"
QT_VERSION="${QT_VERSION:-6.8.3}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
ANDROID_API="${ANDROID_API:-26}"
: "${ANDROID_HOME:?Required Android SDK}"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
QT="$ROOT/out/full-deps/qt/$QT_VERSION/android_arm64_v8a"
QT_HOST="$ROOT/out/full-deps/qt/$QT_VERSION/gcc_64"
OCCT="$ROOT/out/occt-android"
YAML="$ROOT/out/full-deps/yaml-cpp"
ICU="$ROOT/out/full-deps/icu"
SOURCE="$ROOT/.deps/freecad-upstream-full"
BUILDDIR="$ROOT/.build/freecad-upstream-android-arm64"
OUT="$ROOT/out/full-freecad"
mkdir -p "$OUT" "$(dirname "$SOURCE")" "$(dirname "$BUILDDIR")"
test -f "$QT/lib/cmake/Qt6Core/Qt6CoreConfig.cmake"
test -f "$OCCT/lib/libTKBO.so"
test -d "$YAML/lib/cmake/yaml-cpp"
test -f "$ICU/include/unicode/utypes.h"
if [[ ! -d "$SOURCE/.git" ]]; then
  mkdir -p "$SOURCE"
  git -C "$SOURCE" init -q
  git -C "$SOURCE" remote add origin https://github.com/FreeCAD/FreeCAD.git
fi
git -C "$SOURCE" fetch --depth 1 --filter=blob:none origin "$FREECAD_REF" \
    2>&1 | tee "$OUT/freecad-fetch.log"
git -C "$SOURCE" checkout --detach --force FETCH_HEAD
actual="$(git -C "$SOURCE" rev-parse HEAD)"
[[ "$actual" == "$FREECAD_REF" ]] || { echo "Source SHA mismatch: $actual" >&2; exit 1; }
printf '%s\n' "$actual" > "$OUT/full-freecad-source-sha.txt"

echo 'Full upstream FreeCAD configure for Android arm64-v8a — no host ABI fallbacks'
cmake --version | tee "$OUT/cmake-version.txt"
printf '%s\n' "FreeCAD source=$actual" "Qt Android=$QT_VERSION" \
  "OCCT=7.9.3" "NDK=$NDK_VERSION" "Android API=$ANDROID_API" \
  "GUI=OFF (first full native core target FreeCADBase+FreeCADApp)" \
  "NO HOST LINUX LIBRARIES are permitted as Android link dependencies" \
  > "$OUT/full-build-inputs.txt"

occtConfig="$(find "$OCCT" -iname 'OpenCASCADEConfig.cmake' -print -quit)"
if [[ -n "$occtConfig" ]]; then
  echo "OCCT cmake package found: $occtConfig" | tee -a "$OUT/full-build-inputs.txt"
fi
# Build FULL upstream FreeCAD CMake project, initially headless to eliminate
# desktop Coin3D/PySide/QtWidgets, but NOT the FreeCADApp document engine.
# No mock libraries, no substitution of glibc/host Qt/ICU for Android.
set -o pipefail
cmake -S "$SOURCE" -B "$BUILDDIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$ANDROID_API" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$OUT/installed" \
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
  -DCMAKE_PREFIX_PATH="$QT;$OCCT;$YAML;$ICU" \
  -DQt6_DIR="$QT/lib/cmake/Qt6" \
  -DQT_HOST_PATH="$QT_HOST" \
  -DCMAKE_FIND_ROOT_PATH="$QT;$OCCT;$YAML;$ICU" \
  -DICU_ROOT="$ICU" \
  -DICU_INCLUDE_DIR="$ICU/include" \
  -DICU_UC_LIBRARY="$ICU/lib/libicuuc.so" \
  -DICU_I18N_LIBRARY="$ICU/lib/libicui18n.so" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DFREECAD_USE_CCACHE=ON \
  -DBUILD_GUI=OFF \
  -DBUILD_FEM=OFF -DBUILD_FEM_NETGEN=OFF \
  -DBUILD_MESH_PART=OFF -DBUILD_ROBOT=OFF \
  -DBUILD_FLAT_MESH=OFF -DBUILD_OPENSCAD=OFF \
  -DBUILD_BIM=OFF -DBUILD_ASSEMBLY=OFF \
  -DBUILD_TEST=OFF -DENABLE_DEVELOPER_TESTS=OFF \
  -DBUILD_WEB=OFF -DBUILD_DESIGNER_PLUGIN=OFF \
  -DFREECAD_USE_EXTERNAL_SMESH=OFF \
  2>&1 | tee "$OUT/full-cmake-configure.log"

echo 'CMake configure passed: now compile genuine upstream FreeCADBase and FreeCADApp'
cmake --build "$BUILDDIR" --parallel 4 --target FreeCADBase FreeCADApp \
  2>&1 | tee "$OUT/full-core-compile.log"
for name in FreeCADBase FreeCADApp; do
  path="$(find "$BUILDDIR" -type f -name "lib${name}.so*" -print -quit)"
  test -n "$path" || { echo "Missing real ${name} Android library" >&2; exit 1; }
  readelf -h "$path" | grep -q 'Machine:.*AArch64'
  readelf -h "$path" | grep -q 'Type:.*DYN'
  ! readelf -d "$path" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
  sha256sum "$path" | tee -a "$OUT/full-core-sha256.txt"
done
echo 'FULL FreeCADBase + FreeCADApp Android ARM64: BUILD PASS' | tee "$OUT/full-core-result.txt"
