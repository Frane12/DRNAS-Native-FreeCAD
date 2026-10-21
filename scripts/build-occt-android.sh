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
  -DBUILD_MODULE_DataExchange=OFF \
  -DBUILD_MODULE_ApplicationFramework=OFF \
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
