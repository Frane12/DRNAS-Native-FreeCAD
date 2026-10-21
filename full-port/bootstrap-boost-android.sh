#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ANDROID_HOME:?Android SDK missing}"
NDK="$ANDROID_HOME/ndk/${NDK_VERSION:-27.2.12479018}"
SRC="$ROOT/.deps/boost-1.88.0"
BUILD="$ROOT/.build/boost-android"
PREFIX="$ROOT/out/full-deps/boost"
LOG="$ROOT/out/full-freecad"
mkdir -p "$SRC" "$LOG"
if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
  curl -fL --retry 3 https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.tar.xz -o "$ROOT/.deps/boost-cmake.tar.xz"
  sha256sum "$ROOT/.deps/boost-cmake.tar.xz" > "$LOG/boost-source-sha256.txt"
  tar -xf "$ROOT/.deps/boost-cmake.tar.xz" -C "$SRC" --strip-components=1
fi
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_CXX_STANDARD=20 \
  -DBOOST_INCLUDE_LIBRARIES="program_options;regex;thread;date_time;iostreams" \
  -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  2>&1 | tee "$LOG/boost-configure.log"
cmake --build "$BUILD" --parallel 4 2>&1 | tee "$LOG/boost-build.log"
cmake --install "$BUILD" 2>&1 | tee "$LOG/boost-install.log"
# Boost.Regex 1.88 is an INTERFACE/header-only target.
test -f "$PREFIX/include/boost/regex.hpp"
# FreeCAD Base::Reader / Persistence require these headers; excluding
# iostreams previously stopped the real core build at 63/354.
test -f "$PREFIX/include/boost/iostreams/categories.hpp"
test -f "$PREFIX/include/boost/iostreams/stream.hpp"
test -f "$PREFIX/lib/cmake/boost_regex-1.88.0/boost_regex-config.cmake"
for name in program_options thread date_time; do
  lib="$(find "$PREFIX/lib" -maxdepth 1 -type f -name "libboost_${name}.so*" -print -quit)"
  test -n "$lib"
  readelf -h "$lib" | grep -q 'Machine:.*AArch64'
  ! readelf -d "$lib" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
done
echo 'PASS: Boost Android AArch64 libraries verified'
