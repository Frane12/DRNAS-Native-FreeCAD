#!/usr/bin/env bash
# Native Android FreeType and HarfBuzz, linked into genuine upstream Part.so.
# Use static PIC archives to avoid installing GNU/Linux-style versioned ELF
# SONAMEs into Android's APK namespace. No stubbed CAD text geometry.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PREFIX="$ROOT/out/full-deps/font-text"
WORK="$ROOT/.build/freecad-android-font-text"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
test -f "$NDK/build/cmake/android.toolchain.cmake"
mkdir -p "$PREFIX" "$WORK"
FT_REV=42608f77f20749dd6ddc9e0536788eaad70ea4b5
HB_REV=7b27c8edd46c674e01dd226fa9e1aa7549f5c436

if [[ ! -d "$WORK/freetype/.git" ]]; then
  git clone --depth 1 --branch VER-2-13-3 --single-branch \
    https://github.com/freetype/freetype.git "$WORK/freetype"
fi
test "$(git -C "$WORK/freetype" rev-parse HEAD)" = "$FT_REV"
cmake -S "$WORK/freetype" -B "$WORK/freetype-build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$ANDROID_API" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON \
  -DFT_DISABLE_PNG=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON
cmake --build "$WORK/freetype-build" --parallel 4 --target install
test -s "$PREFIX/include/freetype2/ft2build.h"
test -s "$PREFIX/lib/libfreetype.a"

if [[ ! -d "$WORK/harfbuzz/.git" ]]; then
  git clone --depth 1 --branch 10.2.0 --single-branch \
    https://github.com/harfbuzz/harfbuzz.git "$WORK/harfbuzz"
fi
test "$(git -C "$WORK/harfbuzz" rev-parse HEAD)" = "$HB_REV"
cmake -S "$WORK/harfbuzz" -B "$WORK/harfbuzz-build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$ANDROID_API" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF -DHB_BUILD_SUBSET=OFF -DHB_BUILD_UTILS=OFF \
  -DHB_HAVE_CAIRO=OFF -DHB_HAVE_FREETYPE=OFF \
  -DHB_HAVE_GLIB=OFF -DHB_HAVE_GOBJECT=OFF -DHB_HAVE_ICU=OFF \
  -DHB_HAVE_GRAPHITE2=OFF -DHB_HAVE_INTROSPECTION=OFF \
  -DPython3_EXECUTABLE="$(command -v python3)"
cmake --build "$WORK/harfbuzz-build" --parallel 4 --target install
test -s "$PREFIX/include/harfbuzz/hb.h"
test -s "$PREFIX/lib/libharfbuzz.a"

# Both static archives must contain genuine Android AArch64 object files.
for lib in libfreetype.a libharfbuzz.a; do
  readelf -h "$PREFIX/lib/$lib" | awk '
    /Machine:/ { if ($0 !~ /AArch64/) bad=1; count++ }
    END { if (bad || !count) exit 1 }'
  sha256sum "$PREFIX/lib/$lib"
done
echo 'PASS: FreeType and HarfBuzz compiled as Android AArch64 PIC libraries.'
