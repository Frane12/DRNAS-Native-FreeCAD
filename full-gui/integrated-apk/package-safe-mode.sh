#!/usr/bin/env bash
# QtLoader loads QT_ANDROID_EXTRA_LIBS before C++ main. A crash there leaves no
# in-app diagnostics. Install FreeCADGui/Coin3D into the signed APK after
# androiddeployqt WITHOUT registering them as QtLoader startup libraries.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STAGE="$ROOT/.build/freecad-integration-staging/native-libs"
OUT="$ROOT/out/integrated-freecad-qtapk"
APK_BUILD="$ROOT/.build/freecad-native-integration"
SDK_BUILDTOOLS="$ANDROID_HOME/build-tools/35.0.0"
mkdir -p "$OUT"
for lib in FreeCADGui FreeCADApp FreeCADBase Coin GL GLU; do
    test -s "$STAGE/lib${lib}.so"
done
test -x "$SDK_BUILDTOOLS/zipalign"
test -x "$SDK_BUILDTOOLS/apksigner"
command -v zip >/dev/null
command -v unzip >/dev/null
command -v readelf >/dev/null
qt_apk="$(find "$APK_BUILD" -type f -name '*.apk' -print -quit)"
test -n "$qt_apk" && test -s "$qt_apk"
work="$ROOT/.build/freecad-safe-package"
rm -rf "$work"
mkdir -p "$work/bundle/lib/arm64-v8a"
cp "$qt_apk" "$work/unsigned.apk"
unzip -Z1 "$qt_apk" > "$OUT/qtloader-original-apk-files.txt"
names=()
skipped=0
while IFS= read -r -d '' so; do
    base="$(basename "$so")"
    member="lib/arm64-v8a/$base"
    if grep -Fxq "$member" "$OUT/qtloader-original-apk-files.txt"; then
        skipped=$((skipped + 1))
        continue
    fi
    readelf -h "$so" | grep -q 'Machine:.*AArch64'
    readelf -h "$so" | grep -q 'Type:.*DYN'
    cp -L "$so" "$work/bundle/lib/arm64-v8a/$base"
    names+=("$member")
done < <(find "$STAGE" -maxdepth 1 -type f -name 'lib*.so' -print0 | sort -z)
test "${#names[@]}" -gt 0
( cd "$work/bundle" && zip -q -0 "$work/unsigned.apk" "${names[@]}" )
# Support Android's newer 16 KB memory pages for uncompressed native ELF.
"$SDK_BUILDTOOLS/zipalign" -P 16 -f 4 "$work/unsigned.apk" "$work/aligned.apk"
test -f "$HOME/.android/debug.keystore" || {
    echo "::error::Missing Android Gradle debug signing keystore"
    exit 1
}
apk="$OUT/DRNAS-FreeCAD-GuiNative-QtActivity-ARM64-SafeMode-debug.apk"
"$SDK_BUILDTOOLS/apksigner" sign \
    --ks "$HOME/.android/debug.keystore" \
    --ks-key-alias androiddebugkey \
    --ks-pass pass:android --key-pass pass:android \
    --out "$apk" "$work/aligned.apk"
"$SDK_BUILDTOOLS/apksigner" verify --verbose "$apk"
"$SDK_BUILDTOOLS/zipalign" -c -P 16 4 "$apk"
unzip -Z1 "$apk" > "$OUT/apk-files.txt"
for needed in libFreeCADGui.so libFreeCADApp.so libFreeCADBase.so libCoin.so libGL.so libGLU.so; do
    grep -Fx "lib/arm64-v8a/$needed" "$OUT/apk-files.txt"
done
echo "PASS: injected ${#names[@]} deferred ARM64 libraries; retained $skipped Qt/OCCT entries."
echo "SAFE MODE: QtLoader no longer preloads FreeCADGui/Coin3D before main()."
