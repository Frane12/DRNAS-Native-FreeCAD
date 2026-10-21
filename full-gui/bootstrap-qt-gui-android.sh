#!/usr/bin/env bash
# Native Qt Widgets/OpenGLWidgets dependency milestone for upstream FreeCADGui.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT_VERSION="${QT_VERSION:-6.8.3}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
ANDROID_API="${ANDROID_API:-26}"
: "${ANDROID_HOME:?Android SDK required}"
QTROOT="$ROOT/out/full-deps/qt"
QT="$QTROOT/$QT_VERSION/android_arm64_v8a"
HOST="$QTROOT/$QT_VERSION/gcc_64"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
OUT="$ROOT/out/full-gui"
mkdir -p "$OUT" "$ROOT/.build"
test -f "$QT/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake"
test -f "$QT/lib/cmake/Qt6OpenGLWidgets/Qt6OpenGLWidgetsConfig.cmake"
test -f "$QT/lib/cmake/Qt6UiTools/Qt6UiToolsConfig.cmake"
test -f "$HOST/bin/qmake6"

# Add QtSvg to the already cached Qt Android base / tools, not the host ABI.
if [[ ! -f "$QT/lib/cmake/Qt6SvgWidgets/Qt6SvgWidgetsConfig.cmake" ]]; then
  python3 -m venv "$RUNNER_TEMP/aqt-gui"
  "$RUNNER_TEMP/aqt-gui/bin/python" -m pip -q install aqtinstall==3.3.0
  "$RUNNER_TEMP/aqt-gui/bin/aqt" install-qt all_os android "$QT_VERSION" \
    android_arm64_v8a --archives qtsvg -O "$QTROOT" \
    2>&1 | tee "$OUT/qt-svg-android-download.log"
  "$RUNNER_TEMP/aqt-gui/bin/aqt" install-qt linux desktop "$QT_VERSION" \
    linux_gcc_64 --archives qtsvg -O "$QTROOT" \
    2>&1 | tee "$OUT/qt-svg-host-download.log"
fi
test -f "$QT/lib/cmake/Qt6SvgWidgets/Qt6SvgWidgetsConfig.cmake"
test -f "$QT/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake"
cmake -S "$ROOT/full-gui/qt-widgets-shell" \
  -B "$ROOT/.build/freecad-gui-qt-widgets-arm64" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$ANDROID_API" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT" -DCMAKE_FIND_ROOT_PATH="$QT" \
  -DQT_HOST_PATH="$HOST" \
  2>&1 | tee "$OUT/qt-widgets-configure.log"
cmake --build "$ROOT/.build/freecad-gui-qt-widgets-arm64" --parallel 4 \
  2>&1 | tee "$OUT/qt-widgets-build.log"
file="$ROOT/.build/freecad-gui-qt-widgets-arm64/libdrnas_freecad_qt_gui_shell.so"
test -s "$file"
readelf -h "$file" | grep -q 'Machine:.*AArch64'
readelf -h "$file" | grep -q 'Type:.*DYN'
readelf -d "$file" | grep -q 'libQt6Widgets_arm64-v8a.so'
readelf -d "$file" | grep -q 'libQt6OpenGLWidgets_arm64-v8a.so'
! readelf -d "$file" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
cp "$file" "$OUT/"
readelf -d "$file" > "$OUT/qt-gui-native-elf-needed.txt"
sha256sum "$OUT/libdrnas_freecad_qt_gui_shell.so" > "$OUT/qt-gui-sha256.txt"
cat > "$OUT/STATUS.txt" <<'TXT'
Qt6 Android ARM64 Widgets + OpenGLWidgets GUI shell: cross-link VERIFIED.
NOT yet upstream FreeCADGui, QtActivity runtime, or Sketcher/PartDesign.
Qt Android bootstrap, Coin3D, OpenGL/GLU compatibility, PySide/Shiboken
and FreeCAD App document bridge remain necessary.
TXT
echo "PASS: Real Qt6 Widgets / OpenGLWidgets GUI shell cross-linked Android arm64"
