#!/usr/bin/env bash
# Stage real, verified Android FreeCADGui/FreeCADApp/FreeCADBase and Coin3D.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARTIFACT="$ROOT/out/upstream-freecad-gui"
NATIVE="$ARTIFACT/native"
STAGE="$ROOT/.build/freecad-integration-staging/native-libs"
REPORT="$ROOT/out/integrated-freecad-qtapk"
QT_VERSION="${QT_VERSION:-6.8.3}"
QT_LIB="$ROOT/out/full-deps/qt/$QT_VERSION/android_arm64_v8a/lib"
mkdir -p "$STAGE" "$REPORT"
find "$STAGE" -maxdepth 1 -type f -name '*.so' -delete
command -v readelf >/dev/null
test -f "$NATIVE/SHA256SUMS"
(cd "$NATIVE" && sha256sum -c SHA256SUMS)
for essential in FreeCADBase FreeCADApp FreeCADGui Coin; do
    test -s "$NATIVE/lib${essential}.so" || {
        echo "::error::Upstream build artifact lacks lib${essential}.so; rebuild full GUI with bundled Coin staging." >&2
        exit 1
    }
done
for gl in GL GLU; do
    test -s "$ARTIFACT/opengl-compat/lib/lib${gl}.so" || {
        echo "::error::Missing verified Android GL4ES/Mesa ${gl} build." >&2
        exit 1
    }
done
test -d "$QT_LIB"
test -d "$ROOT/out/occt-android/lib"
copy_sos() {
    local prefix="$1" so base
    test -d "$prefix" || { echo "::error::Missing Android SDK path $prefix"; exit 1; }
    while IFS= read -r -d '' so; do
        base="$(basename "$so")"
        if [[ -e "$STAGE/$base" ]]; then
            cmp -s "$so" "$STAGE/$base" || {
                echo "::error::Native library filename collision: $base from $prefix"
                exit 1
            }
        else
            cp -L "$so" "$STAGE/$base"
        fi
    done < <(find "$prefix" -maxdepth 1 \( -type f -o -type l \) -name '*.so' -print0)
}
# Qt libraries are provided by androiddeployqt, not duplicated as extras.
copy_sos "$NATIVE"
copy_sos "$ARTIFACT/opengl-compat/lib"
copy_sos "$ROOT/out/occt-android/lib"
copy_sos "$ROOT/out/full-deps/boost/lib"
copy_sos "$ROOT/out/full-deps/icu/lib"
copy_sos "$ROOT/out/full-deps/python/lib"
copy_sos "$ROOT/out/full-deps/xerces-c/lib"
copy_sos "$ROOT/out/full-deps/yaml-cpp/lib"

# Safely rewrite GL4ES and private ICU dependencies in-place. patchelf
# previously caused invalid PT_LOAD alignment (see device's libCoin TIPx
# crash). This intentionally does not relocate any ELF dynamic string table.
python3 "$ROOT/full-gui/integrated-apk/relocate-android-sonames.py" "$STAGE"

: > "$REPORT/native-needed.txt"
missing=0
for so in "$STAGE"/*.so; do
    readelf -h "$so" | grep -q 'Machine:.*AArch64'
    readelf -h "$so" | grep -q 'Type:.*DYN'
    if readelf -d "$so" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'; then
        echo "::error::Host Linux ABI dependency in $so"
        exit 1
    fi
    while IFS= read -r needed; do
        [[ -n "$needed" ]] || continue
        echo "$(basename "$so"): $needed" >> "$REPORT/native-needed.txt"
        case "$needed" in
            libc.so|libm.so|libdl.so|liblog.so|libz.so|libandroid.so|libEGL.so|libGLESv2.so|libGLESv3.so|libOpenSLES.so|libjnigraphics.so|libvulkan.so|libmediandk.so|libaaudio.so|libnativewindow.so|libatomic.so|libstdc++.so|libc++_shared.so)
                continue ;;
        esac
        [[ -f "$STAGE/$needed" || -f "$QT_LIB/$needed" ]] || {
            echo "::error::Missing native dependency: $(basename "$so") -> $needed"
            missing=1
        }
    done < <(readelf -d "$so" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
done
test "$missing" -eq 0
for essential in FreeCADBase FreeCADApp FreeCADGui Coin GL GLU; do
    test -f "$STAGE/lib${essential}.so"
done
(cd "$STAGE" && sha256sum libFreeCADBase.so libFreeCADApp.so libFreeCADGui.so libCoin.so libGL.so libGLU.so) > "$REPORT/native-libs-sha256.txt"
printf 'FreeCAD ARM64 GUI binary staging PASS; source build: %s\n' "${UPSTREAM_GUI_RUN_ID:-unknown}" > "$REPORT/BUILD-INFO.txt"
echo 'QtActivity runtime probe only: FreeCAD upstream MainWindow is not yet started; editing unverified.' >> "$REPORT/BUILD-INFO.txt"
echo "PASS: $(find "$STAGE" -maxdepth 1 -name '*.so' -type f | wc -l) Android AArch64 native libraries staged."
