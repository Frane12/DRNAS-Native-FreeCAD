#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ANDROID_HOME:?Android SDK required}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
VERSION='0.8.0'
SRC="$ROOT/.deps/yaml-cpp-$VERSION"
BUILD="$ROOT/.build/yaml-cpp-android-arm64"
PREFIX="$ROOT/out/full-deps/yaml-cpp"
OUT="$ROOT/out/full-freecad"
mkdir -p "$OUT" "$PREFIX"
if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
  git clone --depth 1 --branch "$VERSION" https://github.com/jbeder/yaml-cpp.git "$SRC"
fi
git -C "$SRC" rev-parse HEAD > "$OUT/yaml-cpp-source-sha.txt"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DYAML_BUILD_SHARED_LIBS=ON \
  -DYAML_CPP_INSTALL=ON -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF \
  -DYAML_CPP_FORMAT_SOURCE=OFF \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  2>&1 | tee "$OUT/yaml-cpp-configure.log"
cmake --build "$BUILD" --parallel 4 2>&1 | tee "$OUT/yaml-cpp-build.log"
cmake --install "$BUILD" 2>&1 | tee "$OUT/yaml-cpp-install.log"
test -d "$PREFIX/lib/cmake/yaml-cpp"
mapfile -t libs < <(find "$PREFIX/lib" -maxdepth 1 -type f -name 'libyaml-cpp.so*' | sort)
(( ${#libs[@]} >= 1 )) || { echo 'No Android YAML C++ shared library' >&2; exit 1; }
: > "$OUT/yaml-cpp-android-audit.txt"
for lib in "${libs[@]}"; do
  file "$lib" >> "$OUT/yaml-cpp-android-audit.txt"
  readelf -h "$lib" | grep -E 'Class:|Machine:|Type:' >> "$OUT/yaml-cpp-android-audit.txt"
  readelf -h "$lib" | grep -q 'Class:.*ELF64'
  readelf -h "$lib" | grep -q 'Machine:.*AArch64'
  readelf -h "$lib" | grep -q 'Type:.*DYN'
  ! readelf -d "$lib" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
  sha256sum "$lib" >> "$OUT/yaml-cpp-android-audit.txt"
done
echo "PASS: yaml-cpp $VERSION compiled for Android arm64-v8a/Bionic" | tee -a "$OUT/yaml-cpp-android-audit.txt"
