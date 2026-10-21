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
find "$STAGE" -maxdepth 1 -type f -name 'lib*.so' -delete
command -v patchelf >/dev/null
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
    done < <(find "$prefix" -maxdepth 1 \( -type f -o -type l \) -name 'lib*.so' -print0)
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

# GL4ES was built with Linux-compatible libGL.so.1 SONAME although the
# Android APK can only package its native library as libGL.so. Normalize
# the *staged* copy and all AArch64 consumers, preserving upstream artifacts.
test -s "$STAGE/libGL.so"
patchelf --set-soname libGL.so "$STAGE/libGL.so"
for so in "$STAGE"/lib*.so; do
    if readelf -d "$so" | grep -Fq 'Shared library: [libGL.so.1]'; then
        patchelf --replace-needed libGL.so.1 libGL.so "$so"
    fi
done
# Do not allow a versioned name to slip past the dependency audit.
if readelf -d "$STAGE/libFreeCADGui.so" | grep -Fq 'libGL.so.1'; then
    echo "::error::FreeCADGui still depends on unpackagable libGL.so.1"
    exit 1
fi

# Android's system ICU uses incompatible symbols: namespace staged ICU 75.
for so in "$STAGE"/lib*.so; do
    while IFS= read -r needed; do
        [[ "$needed" == libicu*.so.75 ]] || continue
        canonical="${needed%.75}"
        test -f "$STAGE/$canonical" || { echo "::error::ICU library $canonical missing"; exit 1; }
        patchelf --replace-needed "$needed" "$canonical" "$so"
    done < <(readelf -d "$so" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
    case "$(basename "$so")" in
        libicu*.so) patchelf --set-soname "$(basename "$so")" "$so";;
    esac
done
icu_renames=()
for so in "$STAGE"/libicu*.so; do
    [[ -f "$so" ]] || continue
    old="$(basename "$so")"
    new="libdrnas_icu75_${old#libicu}"
    mv "$so" "$STAGE/$new"
    patchelf --set-soname "$new" "$STAGE/$new"
    icu_renames+=("$old:$new")
done
for so in "$STAGE"/lib*.so; do
    for pair in "${icu_renames[@]}"; do
        old="${pair%%:*}"
        new="${pair#*:}"
        if readelf -d "$so" | grep -Fq "Shared library: [$old]"; then
            patchelf --replace-needed "$old" "$new" "$so"
        fi
    done
done
: > "$REPORT/native-needed.txt"
missing=0
for so in "$STAGE"/lib*.so; do
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
echo "PASS: $(find "$STAGE" -maxdepth 1 -name 'lib*.so' -type f | wc -l) Android AArch64 native libraries staged."
