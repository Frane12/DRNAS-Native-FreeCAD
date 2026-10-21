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
# Python 3.14 cannot initialize from libpython alone: encodings, importlib,
# codecs and the rest of the standard library must be in the APK. Ship the
# verified official Android CPython prefix rather than host-system Python.
# Only the Python .py/data files go in python314.zip; native extension modules
# must be extracted to a real directory (zipimport cannot dlopen .so files).
pyhome="$ROOT/out/full-deps/python/lib/python3.14"
test -s "$pyhome/encodings/__init__.py"
test -d "$pyhome/lib-dynload"
mkdir -p "$work/bundle/assets/drnas/python-ext"
python3 - "$pyhome" "$work/bundle/assets/drnas" "$freecad_src/src/Ext/freecad" <<'PY_ANDROID_STDLIB'
from pathlib import Path
import sys
import zipfile

stdlib, dest, freecad_ext = map(Path, sys.argv[1:])
archive = dest / "python314.zip"
count = 0
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED,
                     compresslevel=8, allowZip64=True) as z:
    for source in sorted(stdlib.rglob("*")):
        if not source.is_file() or source.is_symlink():
            continue
        rel = source.relative_to(stdlib)
        if any(p in {"lib-dynload", "site-packages", "__pycache__", "test", "tests"}
               for p in rel.parts):
            continue
        if rel.suffix in {".so", ".pyc", ".pyo", ".a", ".o"}:
            continue
        z.write(source, rel.as_posix())
        count += 1
    # Authentic FreeCAD namespace modules from the exact upstream commit,
    # including __init__.py.template (normally configure_file'd by CMake).
    # The Android entrypoint exports PATH_TO_FREECAD_LIBDIR at runtime, which
    # the original upstream package checks before its build-time fallback.
    init = (freecad_ext / "__init__.py.template").read_text()
    if "import FreeCAD as app" not in init or "from pkgutil import extend_path" not in init:
        raise SystemExit("Upstream freecad namespace initializer changed")
    init = init.replace("${FREECAD_LIBRARY_INSTALL_DIR}", ".")
    z.writestr("freecad/__init__.py", init)
    modules = sorted(freecad_ext.glob("*.py"))
    if not modules or not any(p.name == "utils.py" for p in modules):
        raise SystemExit("Upstream freecad Python package incomplete")
    for module in modules:
        z.write(module, "freecad/" + module.name)
    if "freecad/__init__.py" not in z.namelist():
        raise SystemExit("Embedded FreeCAD namespace package is missing")
    print(f"PACKAGED pinned upstream 'freecad' namespace and {len(modules)} Python modules")
    if "encodings/__init__.py" not in z.namelist():
        raise SystemExit("CRITICAL: bundled Python archive lacks encodings")
print(f"PACKAGED {count} real Android Python 3.14 stdlib sources/data into {archive}")
modules = sorted((stdlib / "lib-dynload").glob("*.so"))
if not modules:
    raise SystemExit("CRITICAL: official Android Python lib-dynload is empty")
manifest = dest / "python-ext-manifest.txt"
manifest.write_text("".join(p.name + "\n" for p in modules))
for extension in modules:
    output = dest / "python-ext" / extension.name
    output.write_bytes(extension.read_bytes())
print(f"PACKAGED {len(modules)} Android CPython binary extension modules")
PY_ANDROID_STDLIB
# The packaging script is intentionally strict: missing/host ABI stdlib
# binaries must fail CI, not ship a GUI that dies silently on device.
while IFS= read -r extension; do
    test -n "$extension" || continue
    module="$work/bundle/assets/drnas/python-ext/$extension"
    test -s "$module"
    readelf -h "$module" | grep -q 'Machine:.*AArch64'
    ! readelf -d "$module" | grep -Eq 'libc\.so\.6|ld-linux'
done < "$work/bundle/assets/drnas/python-ext-manifest.txt"
# FreeCAD 1.x executes 'import freecad' (lower-case namespace package)
# during FreeCADInit / FreeCADGuiInit. libFreeCADApp.so alone does not provide
# this pure-Python package. Fetch the *pinned original upstream sources* and
# stage them inside the existing embedded Python 3.14 zip.
FREECAD_REF="d4ffc7f725973e073907746e88d320d3e438fd34"
freecad_src="$work/freecad-upstream-python"
git -C "$work" init -q freecad-upstream-python
git -C "$freecad_src" remote add origin https://github.com/FreeCAD/FreeCAD.git
git -C "$freecad_src" sparse-checkout init --cone
git -C "$freecad_src" sparse-checkout set src/Ext/freecad
git -C "$freecad_src" fetch -q --depth 1 --filter=blob:none origin "$FREECAD_REF"
git -C "$freecad_src" checkout -q --detach FETCH_HEAD
test "$(git -C "$freecad_src" rev-parse HEAD)" = "$FREECAD_REF"
test -s "$freecad_src/src/Ext/freecad/__init__.py.template"
test -s "$freecad_src/src/Ext/freecad/utils.py"

test "${#names[@]}" -gt 0
# Original QtActivity APK uses compressed JNI libs (Gradle extracts them).
# Keep the same packaging policy. Uncompressed debug DSOs ballooned to >550 MB.
( cd "$work/bundle" && zip -q -9 "$work/unsigned.apk" "${names[@]}" )
# Add embedded CPython standard library/extensions as Android assets, without
# eager-loading anything into QtLoader before the user's GUI button click.
( cd "$work/bundle" && zip -q -6 -r "$work/unsigned.apk" assets/drnas )

# Support Android's newer 16 KB memory pages for uncompressed native ELF.
"$SDK_BUILDTOOLS/zipalign" -P 16 -f 4 "$work/unsigned.apk" "$work/aligned.apk"
# Official Qt CMake/androiddeployqt may sign using a temporary local
# debug key without creating ~/.android/debug.keystore on GitHub runners.
# Generate an ephemeral, clearly-debug key if none exists. It is NEVER
# committed, uploaded or reused as a release signing identity.
if [[ ! -f "$HOME/.android/debug.keystore" ]]; then
    mkdir -p "$HOME/.android"
    keytool -genkeypair -noprompt \
        -keystore "$HOME/.android/debug.keystore" \
        -alias androiddebugkey -storepass android -keypass android \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=DRNAS Android Debug,O=DRNAS,C=HR" >/dev/null
    echo "Generated temporary Android DEBUG signing key for this CI build."
fi
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
for required_asset in assets/drnas/python314.zip assets/drnas/python-ext-manifest.txt; do
    grep -Fx "$required_asset" "$OUT/apk-files.txt"
done
echo "SAFE MODE: QtLoader no longer preloads FreeCADGui/Coin3D before main()."
