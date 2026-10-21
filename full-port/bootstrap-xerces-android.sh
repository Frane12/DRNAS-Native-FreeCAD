#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ANDROID_HOME:?Android SDK missing}"
VERSION="3.2.5"
SRC="$ROOT/.deps/xerces-c-$VERSION"
BUILD="$ROOT/.build/xerces-c-android"
PREFIX="$ROOT/out/full-deps/xerces-c"
ICU="$ROOT/out/full-deps/icu"
LOG="$ROOT/out/full-freecad"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
mkdir -p "$LOG" "$PREFIX"
if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
  git clone --depth 1 --branch v3.2.5 https://github.com/apache/xerces-c.git "$SRC"
fi
git -C "$SRC" rev-parse HEAD > "$LOG/xerces-c-source-sha.txt"
# Xerces 3.2.5 unconditionally sets C++14, shadowing the CLI cache value.
# ICU 75 public headers require C++17. Patch that exact upstream default.
if grep -q '^set(CMAKE_CXX_STANDARD 14)
test -f "$ICU/lib/libicuuc.so"
test -f "$ICU/lib/libicudata.so"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_FIND_ROOT_PATH="$ICU" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_PREFIX_PATH="$ICU" \
  -DICU_ROOT="$ICU" -DICU_INCLUDE_DIR="$ICU/include" \
  -DICU_UC_LIBRARY="$ICU/lib/libicuuc.so" \
  -DICU_DATA_LIBRARY="$ICU/lib/libicudata.so" \
  -Dtranscoder=icu -Dnetwork=OFF \
  -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  2>&1 | tee "$LOG/xerces-c-configure.log"
cmake --build "$BUILD" --parallel 4 2>&1 | tee "$LOG/xerces-c-build.log"
cmake --install "$BUILD" 2>&1 | tee "$LOG/xerces-c-install.log"
lib="$(find "$PREFIX/lib" -maxdepth 1 -type f -name 'libxerces-c*.so*' -print -quit)"
test -n "$lib" || { echo 'Xerces-C Android library missing' >&2; exit 1; }
test -f "$PREFIX/include/xercesc/util/XercesVersion.hpp"
readelf -h "$lib" | grep -q 'Class:.*ELF64'
readelf -h "$lib" | grep -q 'Machine:.*AArch64'
readelf -h "$lib" | grep -q 'Type:.*DYN'
! readelf -d "$lib" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
file "$lib" | tee "$LOG/xerces-c-elf-audit.txt"
readelf -d "$lib" | grep NEEDED | tee -a "$LOG/xerces-c-elf-audit.txt"
sha256sum "$lib" | tee -a "$LOG/xerces-c-elf-audit.txt"
echo "PASS: Xerces-C $VERSION Android arm64/Bionic built and verified" | tee -a "$LOG/xerces-c-elf-audit.txt"
 "$SRC/CMakeLists.txt"; then
  sed -i 's/^set(CMAKE_CXX_STANDARD 14)$/set(CMAKE_CXX_STANDARD 17)/' "$SRC/CMakeLists.txt"
fi
grep -q '^set(CMAKE_CXX_STANDARD 17)
test -f "$ICU/lib/libicuuc.so"
test -f "$ICU/lib/libicudata.so"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_FIND_ROOT_PATH="$ICU" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_PREFIX_PATH="$ICU" \
  -DICU_ROOT="$ICU" -DICU_INCLUDE_DIR="$ICU/include" \
  -DICU_UC_LIBRARY="$ICU/lib/libicuuc.so" \
  -DICU_DATA_LIBRARY="$ICU/lib/libicudata.so" \
  -Dtranscoder=icu -Dnetwork=OFF \
  -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  2>&1 | tee "$LOG/xerces-c-configure.log"
cmake --build "$BUILD" --parallel 4 2>&1 | tee "$LOG/xerces-c-build.log"
cmake --install "$BUILD" 2>&1 | tee "$LOG/xerces-c-install.log"
lib="$(find "$PREFIX/lib" -maxdepth 1 -type f -name 'libxerces-c*.so*' -print -quit)"
test -n "$lib" || { echo 'Xerces-C Android library missing' >&2; exit 1; }
test -f "$PREFIX/include/xercesc/util/XercesVersion.hpp"
readelf -h "$lib" | grep -q 'Class:.*ELF64'
readelf -h "$lib" | grep -q 'Machine:.*AArch64'
readelf -h "$lib" | grep -q 'Type:.*DYN'
! readelf -d "$lib" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
file "$lib" | tee "$LOG/xerces-c-elf-audit.txt"
readelf -d "$lib" | grep NEEDED | tee -a "$LOG/xerces-c-elf-audit.txt"
sha256sum "$lib" | tee -a "$LOG/xerces-c-elf-audit.txt"
echo "PASS: Xerces-C $VERSION Android arm64/Bionic built and verified" | tee -a "$LOG/xerces-c-elf-audit.txt"
 "$SRC/CMakeLists.txt"
git -C "$SRC" diff -- CMakeLists.txt > "$LOG/xerces-c-cxx17.patch"
test -f "$ICU/include/unicode/utypes.h"
test -f "$ICU/lib/libicuuc.so"
test -f "$ICU/lib/libicudata.so"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_FIND_ROOT_PATH="$ICU" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_PREFIX_PATH="$ICU" \
  -DICU_ROOT="$ICU" -DICU_INCLUDE_DIR="$ICU/include" \
  -DICU_UC_LIBRARY="$ICU/lib/libicuuc.so" \
  -DICU_DATA_LIBRARY="$ICU/lib/libicudata.so" \
  -Dtranscoder=icu -Dnetwork=OFF \
  -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  2>&1 | tee "$LOG/xerces-c-configure.log"
cmake --build "$BUILD" --parallel 4 2>&1 | tee "$LOG/xerces-c-build.log"
cmake --install "$BUILD" 2>&1 | tee "$LOG/xerces-c-install.log"
lib="$(find "$PREFIX/lib" -maxdepth 1 -type f -name 'libxerces-c*.so*' -print -quit)"
test -n "$lib" || { echo 'Xerces-C Android library missing' >&2; exit 1; }
test -f "$PREFIX/include/xercesc/util/XercesVersion.hpp"
readelf -h "$lib" | grep -q 'Class:.*ELF64'
readelf -h "$lib" | grep -q 'Machine:.*AArch64'
readelf -h "$lib" | grep -q 'Type:.*DYN'
! readelf -d "$lib" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
file "$lib" | tee "$LOG/xerces-c-elf-audit.txt"
readelf -d "$lib" | grep NEEDED | tee -a "$LOG/xerces-c-elf-audit.txt"
sha256sum "$lib" | tee -a "$LOG/xerces-c-elf-audit.txt"
echo "PASS: Xerces-C $VERSION Android arm64/Bionic built and verified" | tee -a "$LOG/xerces-c-elf-audit.txt"
