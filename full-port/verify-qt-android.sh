#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT_VERSION="${QT_VERSION:-6.8.3}"
QT_ROOT="$ROOT/out/full-deps/qt"
QT_ANDROID="$QT_ROOT/$QT_VERSION/android_arm64_v8a"
QT_HOST="$QT_ROOT/$QT_VERSION/gcc_64"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
ANDROID_API="${ANDROID_API:-26}"
: "${ANDROID_HOME:?Required Android SDK}"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
mkdir -p "$ROOT/out/full-freecad"
report="$ROOT/out/full-freecad/qt-android-audit.txt"
: > "$report"
for item in "$QT_ANDROID/lib/cmake/Qt6/Qt6Config.cmake" \
            "$QT_ANDROID/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" \
            "$QT_ANDROID/lib/cmake/Qt6Xml/Qt6XmlConfig.cmake" \
            "$QT_HOST/bin/qmake6"; do
  test -f "$item" || { echo "Missing Qt SDK component $item" | tee -a "$report"; exit 1; }
done
mapfile -t libs < <(find "$QT_ANDROID/lib" -maxdepth 1 -type f -name 'libQt6*.so' | sort)
(( ${#libs[@]} > 0 )) || { echo 'No Qt6 Android shared libraries' >&2; exit 1; }
for lib in "${libs[@]}"; do
  echo "--- $(basename "$lib") ---" >> "$report"
  readelf -h "$lib" | grep -E 'Class:|Machine:|Type:' >> "$report"
  readelf -h "$lib" | grep -q 'Class:.*ELF64'
  readelf -h "$lib" | grep -q 'Machine:.*AArch64'
  readelf -h "$lib" | grep -q 'Type:.*DYN'
  ! readelf -d "$lib" | grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6'
done
cat > "$ROOT/out/full-freecad/qt-smoke.cpp" <<'CPP'
#include <QCoreApplication>
#include <QDomDocument>
extern "C" int drnas_qt_android() {
  QDomDocument xml;
  xml.setContent(QStringLiteral("<drnas platform=\"android\"/>"));
  return xml.documentElement().tagName() == QStringLiteral("drnas") ? 0 : 1;
}
CPP
cat > "$ROOT/out/full-freecad/qt-smoke-CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.22)
project(DRNAS_Android_Qt6Smoke LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Xml)
add_library(drnas_qt_smoke SHARED qt-smoke.cpp)
target_compile_features(drnas_qt_smoke PRIVATE cxx_std_17)
target_link_libraries(drnas_qt_smoke PRIVATE Qt6::Core Qt6::Xml)
CMAKE
mkdir -p "$ROOT/out/full-freecad/qt-smoke-source"
cp "$ROOT/out/full-freecad/qt-smoke.cpp" "$ROOT/out/full-freecad/qt-smoke-source/qt-smoke.cpp"
cp "$ROOT/out/full-freecad/qt-smoke-CMakeLists.txt" "$ROOT/out/full-freecad/qt-smoke-source/CMakeLists.txt"
cmake -S "$ROOT/out/full-freecad/qt-smoke-source" -B "$ROOT/.build/full-freecad-qt-smoke" \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$ANDROID_API" \
  -DCMAKE_PREFIX_PATH="$QT_ANDROID" -DQT_HOST_PATH="$QT_HOST" \
  -DCMAKE_FIND_ROOT_PATH="$QT_ANDROID" \
  2>&1 | tee "$ROOT/out/full-freecad/qt-smoke-configure.log"
cmake --build "$ROOT/.build/full-freecad-qt-smoke" -j 4 \
  2>&1 | tee "$ROOT/out/full-freecad/qt-smoke-build.log"
smoke="$ROOT/.build/full-freecad-qt-smoke/libdrnas_qt_smoke.so"
test -f "$smoke"
readelf -h "$smoke" | grep -q 'Machine:.*AArch64'
readelf -h "$smoke" | grep -q 'Type:.*DYN'
echo "PASS: Qt6 Android ARM64 Core+Xml linked with NDK, $QT_VERSION" | tee -a "$report"
