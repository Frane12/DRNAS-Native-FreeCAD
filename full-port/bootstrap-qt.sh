#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export QT_VERSION="${QT_VERSION:-6.8.3}"
qt="$ROOT/out/full-deps/qt"
mkdir -p "$qt" "$ROOT/out/full-freecad"
python3 -m venv "$RUNNER_TEMP/aqt-python"
"$RUNNER_TEMP/aqt-python/bin/python" -m pip -q install 'aqtinstall==3.3.0'
aqt="$RUNNER_TEMP/aqt-python/bin/aqt"
"$aqt" list-qt linux android --arch "$QT_VERSION" \
  2>&1 | tee "$ROOT/out/full-freecad/qt-architectures.txt"
# The --autodesktop option installs the matching Linux native host tools
# needed by Android Qt6 CMake configuration (moc, rcc).
"$aqt" install-qt linux android "$QT_VERSION" android_arm64_v8a \
  --autodesktop --archives qtbase -O "$qt" \
  2>&1 | tee "$ROOT/out/full-freecad/qt-download.log"
bash "$ROOT/full-port/verify-qt-android.sh"
