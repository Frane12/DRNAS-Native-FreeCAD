#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FREECAD_REF="${FREECAD_REF:-d4ffc7f725973e073907746e88d320d3e438fd34}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
ANDROID_API="${ANDROID_API:-26}"
: "${ANDROID_HOME:?Android SDK missing}"

# Original pinned upstream FreeCAD: real FreeCADBase C++ sources, not a full FreeCADBase library.
SRC="$ROOT/.deps/freecad/src"
OUT="$ROOT/out/freecad-base-probe"
mkdir -p "$SRC/Base" "$OUT/host" "$OUT/android/lib"
for path in src/Base/Base64.cpp src/Base/Base64.h src/FCGlobal.h \
            src/Base/Vector3D.cpp src/Base/Vector3D.h src/Base/Tools.h; do
    target="$ROOT/.deps/freecad/$path"
    if [[ ! -s "$target" ]]; then
        curl --fail --location --silent --show-error --retry 3 \
            --output "$target" "https://raw.githubusercontent.com/FreeCAD/FreeCAD/$FREECAD_REF/$path"
    fi
    test -s "$target"
done
printf '%s\n' "$FREECAD_REF" > "$OUT/UPSTREAM_FREECAD_COMMIT.txt"
sha256sum "$SRC/Base/Base64.cpp" "$SRC/Base/Vector3D.cpp" > "$OUT/source-sha256.txt"

# FreeCAD's upstream Vector3D.cpp imports one Boost math header and an unused Tools.h.
# Host: preserve upstream source, compile with real Boost headers.
echo '--- Linux host test of unmodified pinned FreeCAD Base64 + Vector3D sources ---'
c++ -std=c++20 -O2 -I"$SRC" -I"$SRC/Base" \
  "$SRC/Base/Base64.cpp" "$SRC/Base/Vector3D.cpp" \
  "$ROOT/core-probe/freecadbase_probe.cpp" "$ROOT/core-probe/probe_main.cpp" \
  -o "$OUT/host/freecad-base-probe"
"$OUT/host/freecad-base-probe" | tee "$OUT/host-runtime-result.txt"

# Android: narrowly adapt only one FreeCAD source to avoid importing host Boost/glibc headers.
# Keep the original file intact; save the exact Android-only patch for review.
python3 - "$SRC/Base/Vector3D.cpp" "$OUT/android/Vector3D.android.cpp" <<'PY'
import pathlib, sys
source = pathlib.Path(sys.argv[1]).read_text()
requirements = ['#include <boost/math/special_functions/fpclassify.hpp>', '#include "Tools.h"', 'boost::math::isnan']
for required in requirements:
    if required not in source:
        raise SystemExit(f"Unexpected upstream source change: {required!r}")
adapted = source.replace('#include <boost/math/special_functions/fpclassify.hpp>', '#include <algorithm>')
adapted = adapted.replace('#include "Tools.h"', '// Android portability: Tools.h is not referenced in Vector3D.cpp')
adapted = adapted.replace('boost::math::isnan', 'std::isnan')
pathlib.Path(sys.argv[2]).write_text(adapted)
PY
diff -u "$SRC/Base/Vector3D.cpp" "$OUT/android/Vector3D.android.cpp" \
  > "$OUT/android-vector3d-port.patch" || [[ $? == 1 ]]

echo '--- Compile FreeCADBase source subset natively for Android ARM64 ---'
BIN="$ANDROID_HOME/ndk/$NDK_VERSION/toolchains/llvm/prebuilt/linux-x86_64/bin"
CXX="$BIN/aarch64-linux-android${ANDROID_API}-clang++"
test -x "$CXX"
CACHE=()
if command -v ccache >/dev/null 2>&1; then
    ccache --set-config=max_size="${CCACHE_MAXSIZE:-128M}"
    CACHE=(ccache)
fi
common=(-std=c++20 -O2 -fPIC -I"$SRC" -I"$SRC/Base")
"${CACHE[@]}" "$CXX" "${common[@]}" -c "$SRC/Base/Base64.cpp" -o "$OUT/android/Base64.o"
"${CACHE[@]}" "$CXX" "${common[@]}" -c "$OUT/android/Vector3D.android.cpp" -o "$OUT/android/Vector3D.o"
"${CACHE[@]}" "$CXX" "${common[@]}" -c "$ROOT/core-probe/freecadbase_probe.cpp" \
    -o "$OUT/android/freecadbase_probe.o"
"$CXX" -shared -Wl,--no-undefined -Wl,-soname,libdrnas_freecadbase_probe.so \
  "$OUT/android/Base64.o" "$OUT/android/Vector3D.o" "$OUT/android/freecadbase_probe.o" \
  -o "$OUT/android/lib/libdrnas_freecadbase_probe.so"
"${CACHE[@]}" "$CXX" -std=c++20 -O2 -fPIE \
  -I"$ROOT/core-probe" -c "$ROOT/core-probe/probe_main.cpp" -o "$OUT/android/probe_main.o"
"$CXX" -pie "$OUT/android/probe_main.o" -L"$OUT/android/lib" \
  -Wl,--no-undefined -Wl,-rpath,'$ORIGIN/lib' \
  -ldrnas_freecadbase_probe -o "$OUT/android/freecad-base-probe"
CPP_SHARED="$ANDROID_HOME/ndk/$NDK_VERSION/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
test -f "$CPP_SHARED"
cp "$CPP_SHARED" "$OUT/android/lib/"

report="$OUT/android-elf-audit.txt"
: > "$report"
for target in "$OUT/android/freecad-base-probe" "$OUT/android/lib/"*.so; do
    echo "--- $(basename "$target") ---" >> "$report"
    file "$target" >> "$report"
    readelf -h "$target" | grep -E 'Class:|Machine:|Type:' >> "$report"
    readelf -d "$target" | grep 'NEEDED' >> "$report" || true
    readelf -h "$target" | grep -q 'Class:.*ELF64'
    readelf -h "$target" | grep -q 'Machine:.*AArch64'
    readelf -h "$target" | grep -q 'Type:.*DYN'
    ! readelf -d "$target" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
done
readelf -l "$OUT/android/freecad-base-probe" | grep -F '/system/bin/linker64' >> "$report"
( cd "$OUT/android" && sha256sum freecad-base-probe lib/*.so ) > "$OUT/android-sha256.txt"
cat > "$OUT/android/run-via-adb.txt" <<'EOS'
adb push android /data/local/tmp/drnas-freecad-base
adb shell chmod +x /data/local/tmp/drnas-freecad-base/freecad-base-probe
adb shell 'LD_LIBRARY_PATH=/data/local/tmp/drnas-freecad-base/lib /data/local/tmp/drnas-freecad-base/freecad-base-probe'
EOS
echo 'PASS: FreeCAD original Base64.cpp + Android-adapted Vector3D.cpp compiled to AArch64 ELF; phone execution pending.'
