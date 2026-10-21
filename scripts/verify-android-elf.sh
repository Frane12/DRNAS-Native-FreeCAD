#!/usr/bin/env bash
set -euo pipefail
PREFIX="${1:?OCCT installation prefix required}"
[[ -d "$PREFIX" ]] || { echo "Missing OCCT installation: $PREFIX" >&2; exit 1; }
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out"
REPORT="$ROOT/out/elf-audit.txt"
: > "$REPORT"
mapfile -t libs < <(find "$PREFIX" -type f \( -name '*.so' -o -name '*.so.*' \) | sort)
(( ${#libs[@]} > 0 )) || { echo 'No shared ELF libraries produced' >&2; exit 1; }
for lib in "${libs[@]}"; do
  echo "===== ${lib#"$PREFIX"/} =====" >> "$REPORT"
  file "$lib" >> "$REPORT"
  readelf -h "$lib" | grep -E 'Class:|Type:|Machine:' >> "$REPORT"
  readelf -d "$lib" | grep 'NEEDED' >> "$REPORT" || true
  readelf -h "$lib" | grep -q 'Class:.*ELF64' || { echo "Not 64-bit: $lib" >&2; exit 1; }
  readelf -h "$lib" | grep -q 'Machine:.*AArch64' || { echo "Wrong architecture: $lib" >&2; exit 1; }
  readelf -h "$lib" | grep -q 'Type:.*DYN' || { echo "Not a DYN shared library: $lib" >&2; exit 1; }
  if readelf -d "$lib" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'; then
    echo "Linux/glibc dependency in Android DSO: $lib" >&2; exit 1
  fi
done
( cd "$PREFIX"; find . -type f \( -name '*.so' -o -name '*.so.*' \) -print0 | sort -z | xargs -0 -r sha256sum ) > "$ROOT/out/occt-sha256.txt"
echo "Verified ${#libs[@]} Android AArch64 ELF DSOs" | tee -a "$REPORT"
