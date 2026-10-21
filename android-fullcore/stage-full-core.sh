#!/usr/bin/env bash
# Stage ACTUAL FreeCADBase + FreeCADApp from the verified #20 Android ARM64
# source build. Never rebuild the C++ core during APK packaging.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ABI_DIR="$ROOT/android-smoke/app/src/main/jniLibs/arm64-v8a"
CORE="$ROOT/out/full-core-binary"
QT_VERSION="${QT_VERSION:-6.8.3}"
mkdir -p "$ABI_DIR" "$ROOT/out/android-fullcore"

test -f "$CORE/libFreeCADBase.so"
test -f "$CORE/libFreeCADApp.so"
test -f "$CORE/SHA256SUMS"
( cd "$CORE" && sha256sum -c SHA256SUMS )

# Copy only target Android .so libraries from Android-specific SDK prefixes.
# Qt Linux host SDK and system glibc libraries are NEVER eligible.
copy_libs() {
  local sdk="$1" so basename
  test -d "$sdk" || { echo "Missing Android SDK lib path: $sdk" >&2; exit 1; }
  while IFS= read -r -d '' so; do
    basename="$(basename "$so")"
    if [[ -f "$ABI_DIR/$basename" ]]; then
      cmp -s "$so" "$ABI_DIR/$basename" || {
        echo "Native library name collision: $basename ($sdk)" >&2
        exit 1
      }
    else
      cp -L "$so" "$ABI_DIR/$basename"
    fi
  done < <(find "$sdk" -maxdepth 1 \( -type f -o -type l \) -name 'lib*.so' -print0)
}
copy_libs "$ROOT/out/full-deps/qt/$QT_VERSION/android_arm64_v8a/lib"
copy_libs "$ROOT/out/full-deps/boost/lib"
copy_libs "$ROOT/out/full-deps/icu/lib"
copy_libs "$ROOT/out/full-deps/python/lib"
copy_libs "$ROOT/out/full-deps/xerces-c/lib"
copy_libs "$ROOT/out/full-deps/yaml-cpp/lib"
cp -L "$CORE/libFreeCADBase.so" "$CORE/libFreeCADApp.so" "$ABI_DIR/"

# The upstream ICU Android libraries use versioned ELF SONAMEs (*.so.75),
# but Android Gradle packages APK native libraries under unversioned *.so
# filenames only. Relocate those ICU DT_NEEDED references in staging COPIES.
# Source artifacts remain unchanged and are SHA-256 verified above.
command -v patchelf >/dev/null || { echo "patchelf is needed for ICU SONAME relocation" >&2; exit 1; }
relocations=0
for so in "$ABI_DIR"/lib*.so; do
  while IFS= read -r needed; do
    [[ "$needed" == libicu*.so.75 ]] || continue
    canonical="${needed%.75}"
    test -f "$ABI_DIR/$canonical" || { echo "Missing ICU target: $canonical" >&2; exit 1; }
    patchelf --replace-needed "$needed" "$canonical" "$so"
    relocations=$(( relocations + 1 ))
  done < <(readelf -d "$so" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
  if [[ "$(basename "$so")" == libicu*.so ]] &&
     readelf -d "$so" | grep -q 'Library soname: \[libicu.*\.so\.75\]'; then
    patchelf --set-soname "$(basename "$so")" "$so"
  fi
done
echo "Relocated $relocations ICU 75 ELF DT_NEEDED entries for native APK packaging"

# Reject wrong architecture / glibc and missing DT_NEEDED dependencies.
# Android public NDK platform libraries are provided by the device.
missing=0
: > "$ROOT/out/android-fullcore/needed-dependencies.txt"
for so in "$ABI_DIR"/lib*.so; do
  test -f "$so" || continue
  readelf -h "$so" | grep -q 'Machine:.*AArch64'
  readelf -h "$so" | grep -q 'Type:.*DYN'
  if readelf -d "$so" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'; then
    echo "Host/Linux ELF dependency found in $so" >&2; exit 1
  fi
  while IFS= read -r needed; do
    test -n "$needed" || continue
    echo "$(basename "$so"): $needed" >> "$ROOT/out/android-fullcore/needed-dependencies.txt"
    case "$needed" in
      libc.so|libm.so|libdl.so|liblog.so|libz.so|libandroid.so|libEGL.so|libGLESv2.so|libGLESv3.so|libOpenSLES.so|libjnigraphics.so|libvulkan.so|libmediandk.so|libaaudio.so|libnativewindow.so|libatomic.so|libstdc++.so)
        continue ;;
    esac
    if [[ ! -f "$ABI_DIR/$needed" ]]; then
      echo "Missing packaged dependency: $(basename "$so") requires $needed" >&2
      missing=1
    fi
  done < <(readelf -d "$so" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
done
test "$missing" -eq 0 || exit 1
{
  echo 'DRNAS FreeCAD Native Core Android PRE-ALPHA'
  echo 'ABI: arm64-v8a; Android API 26+'
  echo 'Upstream FreeCAD commit: d4ffc7f725973e073907746e88d320d3e438fd34'
  echo 'Compiled core: GitHub Actions run 39582462578 (full-core BUILD PASS)'
  echo 'App UI: existing OCCT OpenGL ES 2 renderer and STL export.'
  echo 'No FreeCAD desktop GUI or parametric document editor yet.'
} > "$ROOT/out/android-fullcore/BUILD-INFO.txt"
(
  cd "$ABI_DIR"
  sha256sum libFreeCADBase.so libFreeCADApp.so
) > "$ROOT/out/android-fullcore/freecad-core-sha256.txt"
echo "PASS: staged $(find "$ABI_DIR" -maxdepth 1 -type f -name 'lib*.so' | wc -l) native Android libraries including verified FreeCADBase + FreeCADApp"
