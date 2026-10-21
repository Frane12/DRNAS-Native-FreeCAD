#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/out/geometry-arm64"
REPORT="$ROOT/out/geometry-elf-audit.txt"
[[ -f "$DIR/drnas-geometry-check" && -f "$DIR/lib/libdrnas_occt_geometry.so" ]] ||
  { echo 'Missing Android geometry test executable or shared library' >&2; exit 1; }
: > "$REPORT"
for f in "$DIR/drnas-geometry-check" "$DIR"/lib/*.so; do
  echo "===== ${f#"$DIR"/} =====" >> "$REPORT"
  file "$f" >> "$REPORT"
  readelf -h "$f" | grep -E 'Class:|Type:|Machine:' >> "$REPORT"
  readelf -d "$f" | grep 'NEEDED' >> "$REPORT" || true
  readelf -h "$f" | grep -q 'Class:.*ELF64'
  readelf -h "$f" | grep -q 'Machine:.*AArch64'
  readelf -h "$f" | grep -q 'Type:.*DYN'
  if readelf -d "$f" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'; then
    echo "Linux/glibc dependency detected in $f" >&2; exit 1
  fi
done
readelf -l "$DIR/drnas-geometry-check" | grep -F '/system/bin/linker64' >> "$REPORT"
readelf -d "$DIR/lib/libdrnas_occt_geometry.so" | grep -F 'libTK' >> "$REPORT"
( cd "$DIR"; find . -type f \( -name '*.so' -o -name 'drnas-geometry-check' \) -print0 | sort -z | xargs -0 -r sha256sum ) > "$ROOT/out/geometry-sha256.txt"
echo 'Geometry CLI and Android OCCT bridge: AArch64 ELF verified; runtime on physical Android not yet tested' | tee -a "$REPORT"
