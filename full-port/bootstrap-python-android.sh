#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${PYTHON_VERSION_ANDROID:-3.14.8}"
: "${ANDROID_HOME:?Android SDK missing}"
URL="https://www.python.org/ftp/python/${VERSION}/python-${VERSION}-aarch64-linux-android.tar.gz"
OUT="$ROOT/out/full-freecad"
INSTALL="$ROOT/out/full-deps/python"
TMP="$RUNNER_TEMP/drnas-android-python"
mkdir -p "$OUT" "$INSTALL" "$TMP"
curl -fL --retry 4 --retry-delay 5 "$URL" -o "$TMP/python-android.tar.gz" \
  2>&1 | tee "$OUT/python-download.log"
sha256sum "$TMP/python-android.tar.gz" > "$OUT/python-android-package-sha256.txt"
tar tzf "$TMP/python-android.tar.gz" > "$OUT/python-android-package-contents.txt"
mkdir -p "$TMP/extract"
tar xzf "$TMP/python-android.tar.gz" -C "$TMP/extract"
# Official Android CPython release archives include an embeddable prefix.
hdr="$(find "$TMP/extract" -type f -name Python.h -print -quit)"
[[ -n "$hdr" ]] || { echo 'Python.h not found in official Android package' >&2; exit 1; }
pythonInclude="$(dirname "$hdr")"
pythonRoot="$(dirname "$(dirname "$pythonInclude")")"
if [[ ! -d "$pythonRoot/lib" ]]; then
  echo "Detected invalid Python prefix: $pythonRoot" >&2; exit 1
fi
cp -a "$pythonRoot/." "$INSTALL/"
pySo="$(find "$INSTALL/lib" -maxdepth 2 \( -type f -o -type l \) -name 'libpython3.14.so*' -print -quit)"
[[ -n "$pySo" ]] || { echo 'libpython3.14.so missing' >&2; exit 1; }
realSo="$(readlink -f "$pySo")"
readelf -h "$realSo" | grep -q 'Class:.*ELF64'
readelf -h "$realSo" | grep -q 'Machine:.*AArch64'
readelf -h "$realSo" | grep -q 'Type:.*DYN'
! readelf -d "$realSo" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
[[ -f "$INSTALL/include/python3.14/Python.h" ]] || {
  echo 'Unexpected official Python include path' >&2; exit 1;
}
echo "Android Python prefix: $INSTALL"
file "$realSo" | tee "$OUT/python-android-elf-audit.txt"
readelf -d "$realSo" | grep 'NEEDED' | tee -a "$OUT/python-android-elf-audit.txt"
sha256sum "$realSo" | tee -a "$OUT/python-android-elf-audit.txt"

# A real NDK compile/link probe; not just a file-magic check.
NDK="$ANDROID_HOME/ndk/${NDK_VERSION:-27.2.12479018}"
CXX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++"
cat > "$TMP/probe.cpp" <<'CPP'
#include <Python.h>
extern "C" int drnas_embedded_python_probe() {
  return PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION == 14 ? 0 : 1;
}
CPP
"$CXX" -fPIC -shared -std=c++17 \
  -I"$INSTALL/include/python3.14" "$TMP/probe.cpp" \
  -L"$INSTALL/lib" -Wl,--no-undefined -lpython3.14 \
  -o "$OUT/libdrnas_python_android_smoke.so"
readelf -h "$OUT/libdrnas_python_android_smoke.so" | grep -q 'Machine:.*AArch64'
echo "PASS: Official CPython $VERSION Android ARM64 embed SDK; NDK link verified (device execution pending)" \
  | tee -a "$OUT/python-android-elf-audit.txt"
