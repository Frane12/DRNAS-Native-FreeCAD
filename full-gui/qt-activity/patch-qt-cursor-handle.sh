#!/usr/bin/env bash
# Rebuild the original LGPL Qt 6.8.3 class with a null-safe width query.
# Source: qt/qtbase v6.8.3 src/android/jar/src/org/qtproject/qt/android/CursorHandle.java
# Patch the SDK jar before androiddeployqt copies it; do not add duplicate DEX classes.
set -euo pipefail
qt_sdk="$1"
android_jar="$2"
patch_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mapfile -t jars < <(find "$qt_sdk" -name Qt6Android.jar -type f)
test "${#jars[@]}" -eq 1
qt_jar="${jars[0]}"
test -s "$android_jar"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
javac --release 8 -classpath "$android_jar:$qt_jar" -d "$work" \
  "$patch_root/qt-patches/CursorHandle.java"
test -s "$work/org/qtproject/qt/android/CursorHandle.class"
test -s "$work/org/qtproject/qt/android/CursorView.class"
jar uf "$qt_jar" -C "$work" org/qtproject/qt/android/CursorHandle.class \
  -C "$work" org/qtproject/qt/android/CursorView.class
javap -classpath "$qt_jar" -c -p org.qtproject.qt.android.CursorHandle > "$work/bytecode.txt"
python3 - "$work/bytecode.txt" <<'PY'
from pathlib import Path
import sys
code = Path(sys.argv[1]).read_text().split('int width();', 1)[1].split('void updatePosition', 1)[0]
assert 'ifnonnull' in code and 'getIntrinsicWidth' in code, 'CursorHandle null guards missing from SDK jar'
print('PASS: Qt CursorHandle.width safely handles an uninitialized view or missing drawable')
PY
