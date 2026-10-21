#!/usr/bin/env bash
# Build genuine host x86_64 ICU 73.2 for Qt's official host androiddeployqt.
# Ubuntu 24.04 ships ICU 74, whereas Qt 6.8.3 host tools need ICU SONAME 73.
# Do NOT symlink libicu74 -> libicu73: symbols are versioned across releases.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/.build/host-icu73"
PREFIX="$ROOT/out/full-gui/host-icu73"
TARBALL="$ROOT/.deps/icu4c-73_2-src.tgz"
SOURCE="$ROOT/.deps/icu4c-73_2"
mkdir -p "$BUILD" "$PREFIX" "$ROOT/.deps" "$ROOT/out/full-gui"
if [[ ! -f "$SOURCE/icu/source/configure" ]]; then
  curl --fail --location --retry 3 --silent --show-error \
    https://github.com/unicode-org/icu/releases/download/release-73-2/icu4c-73_2-src.tgz \
    -o "$TARBALL"
  mkdir -p "$SOURCE"
  tar -xf "$TARBALL" -C "$SOURCE"
fi
test -x "$SOURCE/icu/source/configure"
(
  cd "$BUILD"
  "$SOURCE/icu/source/configure" \
    --prefix="$PREFIX" \
    --enable-shared --disable-static --disable-tests \
    --disable-samples --disable-extras \
    2>&1 | tee "$ROOT/out/full-gui/host-icu73-configure.log"
  make -j 4 2>&1 | tee "$ROOT/out/full-gui/host-icu73-build.log"
  make install 2>&1 | tee "$ROOT/out/full-gui/host-icu73-install.log"
)
for name in icui18n icuuc icudata; do
  test -f "$PREFIX/lib/lib$name.so.73"
  readelf -h "$PREFIX/lib/lib$name.so.73" | grep -Eq 'Machine:.*Advanced Micro Devices X86-64'
done
echo "PASS: Qt host CLI has authentic ICU 73 dynamic libraries; Android ICU 75 kept separate"
