#!/usr/bin/env bash
set -euo pipefail
: "${ANDROID_HOME:?ANDROID_HOME required}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
ANDROID_API="${ANDROID_API:-26}"
BIN="$ANDROID_HOME/ndk/$NDK_VERSION/toolchains/llvm/prebuilt/linux-x86_64/bin"
CC="$BIN/aarch64-linux-android${ANDROID_API}-clang"
[[ -x "$CC" ]] || { echo "Missing Android clang: $CC" >&2; exit 1; }
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out"
printf 'int main(void) { return 0; }\n' > "$ROOT/out/android-smoke.c"
"$CC" -fPIE -pie "$ROOT/out/android-smoke.c" -o "$ROOT/out/android-smoke"
file "$ROOT/out/android-smoke"
readelf -h "$ROOT/out/android-smoke" | grep -E 'Class:|Type:|Machine:'
readelf -l "$ROOT/out/android-smoke" | grep -F '/system/bin/linker64'
readelf -h "$ROOT/out/android-smoke" | grep -q 'Machine:.*AArch64'
readelf -h "$ROOT/out/android-smoke" | grep -q 'Type:.*DYN'
echo 'Android NDK PIE smoke: PASS'
