#!/usr/bin/env bash
set -euo pipefail
: "${ANDROID_HOME:?Android SDK path ANDROID_HOME required}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
OCCT_REF="${OCCT_REF:-V7_9_3}"
ANDROID_API="${ANDROID_API:-26}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
[[ -f "$TOOLCHAIN" ]] || { echo "Missing NDK toolchain: $TOOLCHAIN" >&2; exit 1; }
SOURCE="${OCCT_SOURCE:-$ROOT/.deps/occt}"
BUILD="$ROOT/.build/occt-android-arm64"
PREFIX="$ROOT/out/occt-android"
mkdir -p "$ROOT/out" "$(dirname "$SOURCE")" "$(dirname "$BUILD")"
if [[ ! -f "$SOURCE/CMakeLists.txt" ]]; then
  git clone --depth 1 --branch "$OCCT_REF" https://github.com/Open-Cascade-SAS/OCCT.git "$SOURCE"
fi
if [[ -d "$SOURCE/.git" ]]; then
  { git -C "$SOURCE" describe --tags --always; git -C "$SOURCE" rev-parse HEAD; } > "$ROOT/out/occt-source-ref.txt"
fi
if command -v ccache >/dev/null 2>&1; then
  ccache --set-config=max_size="${CCACHE_MAXSIZE:-2G}"
  ccache --zero-stats || true
  LAUNCHER_FLAGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
else
  LAUNCHER_FLAGS=()
fi
# The reusable geometry SDK intentionally excludes STEP, IGES and OCAF.
# Actual FreeCAD Part/Sketcher/PartDesign, however, require OCCT DataExchange
# and ApplicationFramework (Interface_Static.hxx, TKXSBase, TKDE*, TKLCAF).
# Enable the complete CAD SDK only for the full GUI pipeline; do not change
# the already-tested geometry-only SDK used by simpler Android consumers.
OCCT_FULL_CAD="${OCCT_FULL_CAD:-0}"
if [[ "$OCCT_FULL_CAD" == 1 ]]; then
  OCCT_MODULE_FLAGS=(-DBUILD_MODULE_DataExchange=ON -DBUILD_MODULE_ApplicationFramework=ON)
else
  OCCT_MODULE_FLAGS=(-DBUILD_MODULE_DataExchange=OFF -DBUILD_MODULE_ApplicationFramework=OFF)
fi
cmake -S "$SOURCE" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM="android-$ANDROID_API" \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_LIBRARY_TYPE=Shared \
  -DBUILD_MODULE_Draw=OFF \
  -DBUILD_MODULE_Visualization=OFF \
  "${OCCT_MODULE_FLAGS[@]}" \
  -DUSE_FREETYPE=OFF \
  -DUSE_FREEIMAGE=OFF \
  -DUSE_TBB=OFF \
  -DUSE_VTK=OFF \
  -DBUILD_DOC_Overview=OFF \
  -DBUILD_DOC_RefMan=OFF \
  "${LAUNCHER_FLAGS[@]}" \
  2>&1 | tee "$ROOT/out/occt-configure.log"
cmake --build "$BUILD" --parallel 4 2>&1 | tee "$ROOT/out/occt-compile.log"
cmake --install "$BUILD" 2>&1 | tee "$ROOT/out/occt-install.log"

if [[ "$OCCT_FULL_CAD" == 1 ]]; then
  for header in Interface_Static.hxx STEPControl_Reader.hxx IGESControl_Reader.hxx TDF_Label.hxx; do
    test -s "$PREFIX/include/opencascade/$header" || {
      echo "::error::Full FreeCAD OCCT SDK is missing $header" >&2; exit 1;
    }
  done
  for lib in TKXSBase TKDESTEP TKDEIGES TKLCAF TKCAF; do
    test -s "$PREFIX/lib/lib${lib}.so" || {
      echo "::error::Full FreeCAD OCCT SDK is missing lib${lib}.so" >&2; exit 1;
    }
  done
  echo 'PASS: Extended OCCT Android DataExchange/ApplicationFramework SDK'
fi
