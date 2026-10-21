#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export QT_VERSION="${QT_VERSION:-6.8.3}"
qt="$ROOT/out/full-deps/qt"
mkdir -p "$qt" "$ROOT/out/full-freecad"
python3 -m venv "$RUNNER_TEMP/aqt-python"
"$RUNNER_TEMP/aqt-python/bin/python" -m pip -q install 'aqtinstall==3.3.0'
aqt="$RUNNER_TEMP/aqt-python/bin/aqt"
"$aqt" list-qt all_os android --arch "$QT_VERSION" \
  2>&1 | tee "$ROOT/out/full-freecad/qt-architectures.txt"
# Qt >= 6.7 has its Android binary archives under the all_os host.
# Linux x86_64 host binaries (moc/rcc) come from a SEPARATE desktop package.
"$aqt" install-qt all_os android "$QT_VERSION" android_arm64_v8a \
  --archives qtbase qttools -O "$qt" \
  2>&1 | tee "$ROOT/out/full-freecad/qt-android-download.log"
"$aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
  --archives qtbase qttools -O "$qt" \
  2>&1 | tee "$ROOT/out/full-freecad/qt-host-download.log"
bash "$ROOT/full-port/verify-qt-android.sh"
