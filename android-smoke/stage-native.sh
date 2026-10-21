#!/usr/bin/env bash
set -euo pipefail
: "${ANDROID_HOME:?ANDROID_HOME required}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
ANDROID_API="${ANDROID_API:-26}"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
CXX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android${ANDROID_API}-clang++"
PREFIX="$ROOT/out/occt-android"
SDK_INC="$PREFIX/include/opencascade"
SDK_LIB="$PREFIX/lib"
DIR="$ROOT/android-smoke/app/src/main/jniLibs/arm64-v8a"
OBJ="$ROOT/out/apk-obj"
mkdir -p "$DIR" "$OBJ"
[[ -f "$SDK_LIB/libTKBO.so" && -f "$SDK_INC/BRepAlgoAPI_Cut.hxx" ]] ||
  { echo "OCCT cache missing; refusing to recompile OCCT" >&2; exit 1; }
[[ -x "$CXX" ]] || { echo "NDK compiler missing" >&2; exit 1; }
CACHE=()
if command -v ccache >/dev/null 2>&1; then
  ccache --set-config=max_size="${CCACHE_MAXSIZE:-128M}"
  CACHE=(ccache)
fi

# Compile separately from link so ccache stores the actual source compilation.
"${CACHE[@]}" "$CXX" -std=c++17 -O2 -fPIC \
  -I"$SDK_INC" -I"$ROOT/native-tests" -c \
  "$ROOT/native-tests/drnas_occt_geometry.cpp" -o "$OBJ/geometry.o"
"$CXX" -shared -Wl,--no-undefined -Wl,-soname,libdrnas_occt_geometry.so \
  -L"$SDK_LIB" "$OBJ/geometry.o" \
  -lTKBO -lTKBool -lTKPrim -lTKMesh -lTKTopAlgo -lTKGeomAlgo \
  -lTKBRep -lTKGeomBase -lTKG3d -lTKG2d -lTKMath -lTKernel \
  -o "$DIR/libdrnas_occt_geometry.so"

"${CACHE[@]}" "$CXX" -std=c++17 -O2 -fPIC \
  -I"$ROOT/native-tests" -c \
  "$ROOT/android-smoke/jni/drnas_smoke_jni.cpp" -o "$OBJ/jni.o"
"$CXX" -shared -Wl,--no-undefined -Wl,-soname,libdrnas_freecad_smoke.so \
  -L"$DIR" "$OBJ/jni.o" -ldrnas_occt_geometry \
  -o "$DIR/libdrnas_freecad_smoke.so"

# Android app loader finds all transitive DSOs in its lib/arm64-v8a namespace.
cp "$SDK_LIB"/libTK*.so "$DIR/"
STDCPP="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
test -f "$STDCPP"
cp "$STDCPP" "$DIR/"
for lib in "$DIR"/*.so; do
    readelf -h "$lib" | grep -q 'Machine:.*AArch64'
    readelf -h "$lib" | grep -q 'Type:.*DYN'
    if readelf -d "$lib" | grep -Eq 'libc\.so\.6|ld-linux'; then
      echo "Not an Android ELF: $lib" >&2; exit 1
    fi
done
echo "Staged $(find "$DIR" -maxdepth 1 -type f -name '*.so' | wc -l) Android native libraries"
