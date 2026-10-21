#!/usr/bin/env bash
set -euo pipefail
: "${ANDROID_HOME:?ANDROID_HOME must point to the Android SDK}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
ANDROID_API="${ANDROID_API:-26}"
NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
CXX="$BIN/aarch64-linux-android${ANDROID_API}-clang++"
PREFIX="$ROOT/out/occt-android"
SDK_INC="$PREFIX/include/opencascade"
SDK_LIB="$PREFIX/lib"
OUT="$ROOT/out/geometry-arm64"
mkdir -p "$OUT/lib"
[[ -x "$CXX" ]] || { echo "Missing Android NDK compiler: $CXX" >&2; exit 1; }
[[ -f "$SDK_INC/BRepAlgoAPI_Cut.hxx" && -f "$SDK_LIB/libTKBO.so" ]] ||
  { echo "Cached OCCT Android headers/libs not found; restore verified phase-1 cache" >&2; exit 1; }

# OCCT is restored from the successful phase-1 install cache; no OCCT rebuild.
LINK_LIBS=(
  -lTKBO -lTKBool -lTKPrim -lTKMesh -lTKTopAlgo
  -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG3d
  -lTKG2d -lTKMath -lTKernel
)
CACHE=()
if command -v ccache >/dev/null 2>&1; then
  ccache --set-config=max_size="${CCACHE_MAXSIZE:-128M}"
  CACHE=(ccache)
fi

"${CACHE[@]}" "$CXX" -std=c++17 -O2 -fPIC -shared \
  -I"$SDK_INC" -I"$ROOT/native-tests" \
  "$ROOT/native-tests/drnas_occt_geometry.cpp" \
  -L"$SDK_LIB" -Wl,--no-undefined -Wl,-soname,libdrnas_occt_geometry.so \
  "${LINK_LIBS[@]}" -o "$OUT/lib/libdrnas_occt_geometry.so"

"${CACHE[@]}" "$CXX" -std=c++17 -O2 -fPIE -pie \
  -I"$ROOT/native-tests" "$ROOT/native-tests/geometry_main.cpp" \
  -L"$OUT/lib" -L"$SDK_LIB" -Wl,--no-undefined \
  -Wl,-rpath,'$ORIGIN/lib' -ldrnas_occt_geometry \
  -o "$OUT/drnas-geometry-check"
# Package all direct and transitive OCCT dependencies for easy adb smoke testing.
cp "$SDK_LIB"/libTK*.so "$OUT/lib/"
CPP_SHARED="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
[[ -f "$CPP_SHARED" ]] || { echo "Missing NDK libc++_shared.so" >&2; exit 1; }
cp "$CPP_SHARED" "$OUT/lib/"
cat > "$OUT/run-on-android.sh" <<'EOS'
#!/system/bin/sh
set -e
DIR=/data/local/tmp/drnas-freecad
export LD_LIBRARY_PATH="$DIR/lib"
exec "$DIR/drnas-geometry-check"
EOS
echo "Geometry smoke build produced: $OUT/drnas-geometry-check"
