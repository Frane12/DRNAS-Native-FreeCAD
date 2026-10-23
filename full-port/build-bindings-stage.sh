#!/usr/bin/env bash
# Host generators run on Linux; every installed runtime is Android ARM64.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
STAGE="${1:?stage required}"
QT_VERSION="${QT_VERSION:-6.8.3}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
ANDROID_API="${ANDROID_API:-26}"
NDK="${ANDROID_HOME:?Android SDK required}/ndk/$NDK_VERSION"
QT="$ROOT/out/full-deps/qt/$QT_VERSION/android_arm64_v8a"
HOSTQT="$ROOT/out/full-deps/qt/$QT_VERSION/gcc_64"
PY="$ROOT/out/full-deps/python"
HOST="$ROOT/out/bindings/shiboken-host"
TARGET="$ROOT/out/bindings/target"
SITE="$TARGET/lib/python3.14/site-packages"
LOGS="$ROOT/out/bindings/logs"
mkdir -p "$LOGS" "$ROOT/.build" "$TARGET"
exec > >(tee "$LOGS/$STAGE.log") 2>&1
trap 'echo "::error::Bindings stage failed: $STAGE (line $LINENO)"' ERR
need() { test -s "$1" || { echo "::error::Missing prerequisite: $1"; exit 1; }; }
audit() {
  local dir="$1" so header dynamic count=0
  while IFS= read -r -d '' so; do
    header="$(readelf -h "$so")"
    grep -q 'Machine:.*AArch64' <<< "$header"
    grep -q 'Type:.*DYN' <<< "$header"
    dynamic="$(readelf -d "$so")"
    if grep -Eq 'libc\.so\.6|ld-linux|libstdc\+\+\.so\.6' <<< "$dynamic"; then
      echo "::error::Desktop runtime dependency: $so"; return 1
    fi
    printf '%s\n%s\n' "$so" "$dynamic" >> "$LOGS/$STAGE-elf.txt"
    count=$((count+1))
  done < <(find "$dir" -type f -name '*.so*' -print0)
  (( count > 0 )) || { echo "::error::No shared libraries in $dir"; return 1; }
}
fetch() {
  local repo="$1" ref="$2" dest="$3"
  if [[ ! -d "$dest/.git" ]]; then
    git init -q "$dest"
    git -C "$dest" remote add origin "https://github.com/$repo.git"
  fi
  git -C "$dest" fetch --depth 1 origin "$ref"
  git -C "$dest" checkout --detach --force FETCH_HEAD
  git -C "$dest" rev-parse HEAD > "$LOGS/$(basename "$dest")-source.txt"
}
# Stable install paths and full install trees preserve CMake packages and headers.
checkpoint() {
  tar czf "$ROOT/out/bindings/$1-sdk.tar.gz" -C "$TARGET" .
  sha256sum "$ROOT/out/bindings/$1-sdk.tar.gz" > "$ROOT/out/bindings/$1-sdk.sha256"
}
if [[ "$STAGE" != sources && "$STAGE" != host-deps ]]; then
  need "$NDK/build/cmake/android.toolchain.cmake"
  need "$PY/include/python3.14/Python.h"
  need "$PY/lib/libpython3.14.so"
  # Do not use grep -q on nm/readelf pipes under pipefail (SIGPIPE).
  symbols="$("$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm" -D --defined-only "$PY/lib/libpython3.14.so")"
  grep -Eq ' [TW] PyType_Ready$' <<< "$symbols"
fi
# This is Shiboken's supported link-library input. Android's linker requires
# CPython explicitly; Linux's implicit symbol lookup rule does not apply.
cross=(
  "-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake"
  -DANDROID_ABI=arm64-v8a "-DANDROID_PLATFORM=android-$ANDROID_API"
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$TARGET"
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  "-DCMAKE_PREFIX_PATH=$QT;$TARGET;$PY" "-DCMAKE_FIND_ROOT_PATH=$QT;$TARGET;$PY"
  "-DQt6_DIR=$QT/lib/cmake/Qt6" "-DQT_HOST_PATH=$HOSTQT"
  "-DPython_ROOT_DIR=$PY" "-DPython_INCLUDE_DIR=$PY/include/python3.14"
  "-DPython_LIBRARY=$PY/lib/libpython3.14.so"
  "-DPython_EXECUTABLE=$(command -v python3)"
  "-DPython_SOABI=cpython-314-aarch64-linux-android"
  "-DSHIBOKEN_PYTHON_LIBRARIES=$PY/lib/libpython3.14.so"
  "-DQFP_PYTHON_HOST_PATH=$(command -v python3)"
  "-DQFP_PYTHON_TARGET_PATH=$PY" "-DQFP_QT_TARGET_PATH=$QT"
  "-DQFP_PYTHON_SITE_PACKAGES=$SITE"
  -DBUILD_TESTS=OFF
)
case "$STAGE" in
  sources)
    fetch pyside/pyside-setup 3ae6c97953d563c33ff630f259716b01b81aa531 .build/pyside-setup
    fetch coin3d/coin 1235c1cae0bd4f48127e5529ce15a10e893eff4d .build/coin
    fetch coin3d/pivy 634a7b5e26b62703b74e58de33d7bd8a534fa386 .build/pivy
    ;;
  host-deps)
    # Qt's official 6.8 host SDK needs ICU73, isolated from the target SDK.
    mkdir -p "$ROOT/.build/icu73"
    curl -fL --retry 4 https://mirrors.edge.kernel.org/deepin/beige/pool/main/i/icu/libicu73_73.2-1deepin1_amd64.deb -o "$ROOT/.build/icu73/icu.deb"
    dpkg-deb -x "$ROOT/.build/icu73/icu.deb" "$ROOT/.build/icu73"
    icu="$(find "$ROOT/.build/icu73" -name 'libicuuc.so.73*' -type f -printf '%h\n' -quit)"
    test -n "$icu"
    export LD_LIBRARY_PATH="$icu:$HOSTQT/lib:${LD_LIBRARY_PATH:-}"
    export LIBRARY_PATH="$icu:${LIBRARY_PATH:-}"
    export LDFLAGS="-Wl,-rpath-link,$icu ${LDFLAGS:-}"
    for variable in LD_LIBRARY_PATH LIBRARY_PATH LDFLAGS; do
      printf '%s=%s\n' "$variable" "${!variable}" >> "$GITHUB_ENV"
    done
    ;;
  host)
    need "$HOSTQT/lib/cmake/Qt6/Qt6Config.cmake"
    ldd "$HOSTQT/lib/libQt6Core.so.6" > "$LOGS/host-qt-ldd.txt"
    if grep -q 'not found' "$LOGS/host-qt-ldd.txt"; then cat "$LOGS/host-qt-ldd.txt"; exit 1; fi
    cmake -S .build/pyside-setup/sources/shiboken6 -B .build/shiboken-host -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOST" \
      -DCMAKE_PREFIX_PATH="$HOSTQT" -DPython_EXECUTABLE="$(command -v python3)" \
      -DSHIBOKEN_BUILD_TOOLS=ON -DSHIBOKEN_BUILD_LIBS=ON -DBUILD_TESTS=OFF
    cmake --build .build/shiboken-host --parallel 4
    cmake --install .build/shiboken-host
    need "$HOST/lib/cmake/Shiboken6Tools/Shiboken6ToolsConfig.cmake"
    "$HOST/bin/shiboken6" --version
    ;;
  shiboken)
    need "$QT/lib/cmake/Qt6/Qt6Config.cmake"
    need "$HOST/lib/cmake/Shiboken6Tools/Shiboken6ToolsConfig.cmake"
    audit "$PY/lib"
    cmake -S .build/pyside-setup/sources/shiboken6 -B .build/shiboken -G Ninja "${cross[@]}" \
      -DSHIBOKEN_BUILD_TOOLS=OFF -DSHIBOKEN_BUILD_LIBS=ON \
      -DShiboken6Tools_DIR="$HOST/lib/cmake/Shiboken6Tools" -DQFP_SHIBOKEN_HOST_PATH="$HOST"
    cmake --build .build/shiboken --parallel 4
    cmake --install .build/shiboken
    need "$TARGET/lib/cmake/Shiboken6/Shiboken6Config.cmake"
    need "$SITE/shiboken6/__init__.py"
    audit "$TARGET"
    checkpoint shiboken
    ;;
  pyside6)
    need "$TARGET/lib/cmake/Shiboken6/Shiboken6Config.cmake"
    need "$HOST/lib/cmake/Shiboken6Tools/Shiboken6ToolsConfig.cmake"
    audit "$TARGET"
    cmake -S .build/pyside-setup/sources/pyside6 -B .build/pyside6-arm64 -G Ninja "${cross[@]}" \
      -DQFP_SHIBOKEN_HOST_PATH="$HOST" -DQFP_SHIBOKEN_TARGET_PATH="$TARGET" \
      -DShiboken6_DIR="$TARGET/lib/cmake/Shiboken6" \
      -DShiboken6Tools_DIR="$HOST/lib/cmake/Shiboken6Tools" \
      -DMODULES='Core;Gui;Widgets;PrintSupport;Sql;Network;Concurrent;OpenGL;OpenGLWidgets;Svg;SvgWidgets;Xml;UiTools'
    cmake --build .build/pyside6-arm64 --parallel 4
    cmake --install .build/pyside6-arm64
    for module in QtCore QtGui QtWidgets QtSvg QtOpenGLWidgets; do
      compgen -G "$SITE/PySide6/$module*.so" > /dev/null
    done
    need "$SITE/PySide6/__init__.py"
    audit "$TARGET"
    checkpoint pyside6
    ;;
  coin3d)
    GL="$ROOT/out/upstream-freecad-gui/opengl-compat"
    need "$GL/include/GL/gl.h"
    need "$GL/lib/libGL.so"
    need "$GL/lib/libGLU.so"
    audit "$GL/lib"
    cmake -S .build/coin -B .build/coin-arm64 -G Ninja "${cross[@]}" \
      -DCOIN_BUILD_TESTS=OFF -DCOIN_BUILD_DOCUMENTATION=OFF \
      -DCOIN_BUILD_GLX=OFF -DCOIN_BUILD_EGL=OFF \
      -DOPENGL_INCLUDE_DIR="$GL/include" -DOPENGL_gl_LIBRARY="$GL/lib/libGL.so" \
      -DOPENGL_glu_LIBRARY="$GL/lib/libGLU.so" -DOPENGL_opengl_LIBRARY="$GL/lib/libGL.so" \
      -DCMAKE_SHARED_LINKER_FLAGS=-lEGL
    cmake --build .build/coin-arm64 --parallel 4
    cmake --install .build/coin-arm64
    cp -a "$GL/include/." "$TARGET/include/"
    cp -a "$GL/lib/." "$TARGET/lib/"
    need "$TARGET/include/Inventor/SoDB.h"
    audit "$TARGET"
    checkpoint coin3d
    ;;
  pivy)
    need "$TARGET/include/Inventor/SoDB.h"
    command -v swig
    # Run helpers on the host, but discover Development from the target.
    # Combined Interpreter+Development discovery rejects differing host ABI.
    python3 - <<'PY'
from pathlib import Path
p = Path('.build/pivy/CMakeLists.txt')
s = p.read_text()
a = 'find_package(Python REQUIRED COMPONENTS Interpreter Development)'
b = 'find_package(Python REQUIRED COMPONENTS Development)'
if a in s:
    s = s.replace(a, b)
else:
    assert b in s, 'Pivy Python discovery changed'
p.write_text(s)
p = Path('.build/pivy/interfaces/CMakeLists.txt')
s = p.read_text()
a = 'target_link_libraries(coin PUBLIC Coin::Coin Python::Module)'
b = 'target_link_libraries(coin PUBLIC Coin::Coin Python::Module Python::Python)'
if a in s:
    s = s.replace(a, b)
else:
    assert b in s, 'Pivy coin target changed'
p.write_text(s)
PY
    cmake -S .build/pivy -B .build/pivy-arm64 -G Ninja "${cross[@]}" \
      -DPIVY_Python_SITEARCH="$SITE" -DPIVY_GENERATE_PYI=OFF \
      -DCMAKE_DISABLE_FIND_PACKAGE_SoQt=ON -DCMAKE_MODULE_LINKER_FLAGS=-Wl,--no-undefined
    cmake --build .build/pivy-arm64 --parallel 2
    cmake --install .build/pivy-arm64
    need "$SITE/pivy/__init__.py"
    need "$SITE/pivy/coin.py"
    compgen -G "$SITE/pivy/_coin*.so" > /dev/null
    audit "$TARGET"
    checkpoint pivy
    ;;
  bundle)
    # Runtime layout consumed by the FreeCAD integrator, plus full SDK tarballs.
    mkdir -p out/bindings/runtime/python-bindings out/bindings/runtime/native
    cp -a "$SITE/PySide6" "$SITE/shiboken6" "$SITE/pivy" out/bindings/runtime/python-bindings/
    cp -a full-gui/upstream/python-compat out/bindings/runtime/
    find "$TARGET/lib" -maxdepth 1 -name '*.so*' -exec cp -L {} out/bindings/runtime/native/ \;
    # Supply shared libc++ from exactly the NDK used to compile the bindings.
    cp "$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" out/bindings/runtime/native/
    audit out/bindings/runtime
    python3 - <<'PY'
from pathlib import Path
root = Path('out/bindings/runtime/python-bindings')
for package, modules in {'PySide6': ['QtCore', 'QtGui', 'QtWidgets'], 'shiboken6': ['Shiboken'], 'pivy': ['_coin']}.items():
    assert (root/package/'__init__.py').is_file(), package
    for module in modules:
        assert list((root/package).glob(module+'*.so')), (package,module)
PY
    tar czf out/bindings/android-python-bindings-arm64.tar.gz -C out/bindings/runtime .
    sha256sum out/bindings/android-python-bindings-arm64.tar.gz > out/bindings/android-python-bindings-arm64.sha256
    echo 'ARM64 bindings cross-compiled and ELF audited. Device imports and APK integration pending.' > out/bindings/STATUS.txt
    ;;
  *) echo "Unknown stage: $STAGE" >&2; exit 2 ;;
esac
