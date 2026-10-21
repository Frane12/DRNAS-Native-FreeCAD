#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ANDROID_HOME:?Android SDK required}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
ANDROID_API="${ANDROID_API:-26}"
VERSION="75.1"
SOURCE="$ROOT/.deps/icu-75.1"
ICUSRC="$SOURCE/icu4c/source"
HOST="$ROOT/.build/icu-75.1-host"
TARGET="$ROOT/.build/icu-75.1-android"
PREFIX="$ROOT/out/full-deps/icu"
OUT="$ROOT/out/full-freecad"
mkdir -p "$HOST" "$TARGET" "$PREFIX" "$OUT"
if [[ ! -f "$ICUSRC/configure" ]]; then
  git clone --filter=blob:none --depth 1 --branch release-75-1 \
    https://github.com/unicode-org/icu.git "$SOURCE"
fi
git -C "$SOURCE" rev-parse HEAD > "$OUT/icu-source-sha.txt"
BIN="$ANDROID_HOME/ndk/$NDK_VERSION/toolchains/llvm/prebuilt/linux-x86_64/bin"
export CC="$BIN/aarch64-linux-android${ANDROID_API}-clang"
export CXX="$BIN/aarch64-linux-android${ANDROID_API}-clang++"
export AR="$BIN/llvm-ar"
export RANLIB="$BIN/llvm-ranlib"
export STRIP="$BIN/llvm-strip"
test -x "$CC" && test -x "$CXX"

# ICU's data builders (genccode, pkgdata, etc.) must execute on the Linux host.
# They are NOT the same ELF files that will be installed for Android.
echo 'Building ICU host-side generators (Linux x86_64)'
(
  cd "$HOST"
  env -u CC -u CXX -u AR -u RANLIB -u STRIP \
    "$ICUSRC/configure" --prefix="$OUT/icu-host" \
    --disable-tests --disable-samples --disable-extras \
    2>&1 | tee "$OUT/icu-host-configure.log"
  make -j 4 2>&1 | tee "$OUT/icu-host-build.log"
)

echo 'Cross-compiling real ICU uc+i18n+data libraries for Android/Bionic'
(
  cd "$TARGET"
  "$ICUSRC/configure" --host=aarch64-linux-android \
    --with-cross-build="$HOST" \
    --prefix="$PREFIX" \
    --enable-shared --disable-static \
    --disable-tests --disable-samples --disable-extras \
    --with-data-packaging=library \
    2>&1 | tee "$OUT/icu-android-configure.log"
  make -j 4 2>&1 | tee "$OUT/icu-android-build.log"
  make install 2>&1 | tee "$OUT/icu-android-install.log"
)
for name in icuuc icui18n icudata; do
  lib="$(find "$PREFIX/lib" -maxdepth 1 -type f -name "lib${name}.so.*" -print -quit)"
  [[ -f "$lib" ]] || { echo "Missing ICU Android library: ${name}" >&2; exit 1; }
  readelf -h "$lib" | grep -q 'Class:.*ELF64'
  readelf -h "$lib" | grep -q 'Machine:.*AArch64'
  readelf -h "$lib" | grep -q 'Type:.*DYN'
  ! readelf -d "$lib" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
  file "$lib" | tee -a "$OUT/icu-android-audit.txt"
  sha256sum "$lib" | tee -a "$OUT/icu-android-audit.txt"
done
echo "PASS: ICU $VERSION uc+i18n+data are real Android AArch64 DSOs" | tee -a "$OUT/icu-android-audit.txt"
