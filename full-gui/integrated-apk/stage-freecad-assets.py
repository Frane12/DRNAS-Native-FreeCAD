#!/usr/bin/env python3
"""Repackage original FreeCAD GUI resources as Android assets with path manifest."""
from pathlib import Path
import shutil
import sys
import os
import hashlib

REQUIRED = {
    "Mod/Part/InitGui.py", "Mod/Sketcher/InitGui.py",
    "Mod/PartDesign/InitGui.py",
    "data/Gui/Stylesheets/overlay/icons/taskshow.svg",
    "share/Mod/Material/Resources/Materials/Appearance/Default.FCMat",
    "share/Mod/Material/Resources/Materials/Standard/Default.FCMat",
    "share/Mod/Material/Resources/Models/Rendering/BasicRendering.yml",
}


def main():
    if len(sys.argv) != 4:
        raise SystemExit("Usage: stage-freecad-assets.py upstream-resources asset-dir relocated-ELFs")
    source, bundle, native = map(Path, sys.argv[1:])
    if not source.is_dir():
        raise SystemExit("FreeCAD native artifact is missing the upstream GUI assets")
    names = []
    for file in sorted(source.rglob("*")):
        if not file.is_file() or file.is_symlink():
            continue
        relative = file.relative_to(source)
        if any(part in {".", ".."} for part in relative.parts):
            raise SystemExit(f"Unsafe GUI resource path: {relative}")
        target = bundle / "freecad" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)
        names.append(relative.as_posix())
    # Android's native library extractor can ignore Python extension ELF names
    # such as Part.so (no "lib" prefix). Ship the exact relocated binaries in
    # app-private FreeCAD/lib as a reliable CPython and module-loader fallback.
    # Explicitly labeled prebuilt LAB: previous archive predates _PartDesign.so staging.
    extensions = ["Part", "PartGui", "Sketcher", "SketcherGui"]
    if os.environ.get("DRNAS_PARTIAL_GUI_LAB") != "1":
        extensions.extend(["Materials", "MatGui", "_PartDesign", "PartDesignGui"])
    for module in extensions:
        name = module + ".so"
        library = native / name
        if not library.is_file() or library.stat().st_size < 8192:
            raise SystemExit(f"Missing verified Android GUI extension: {library}")
        target = bundle / "freecad" / "lib" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(library, target)
        names.append("lib/" + name)
    runtime = bundle / "freecad" / "lib" / "drnas_android_runtime.py"
    shutil.copyfile(Path(__file__).with_name("drnas_android_runtime.py"), runtime)
    names.append("lib/drnas_android_runtime.py")
    # Include payload hashes in a bundled asset so a same-name binary/script
    # update changes the manifest identity and invalidates on-device extraction.
    hashes = "\n".join(
        hashlib.sha256((bundle / "freecad" / name).read_bytes()).hexdigest() + "  " + name
        for name in sorted(names)
    ) + "\n"
    digest = hashlib.sha256(hashes.encode()).hexdigest()
    identity = "bundle-" + digest + ".sha256"
    (bundle / "freecad" / identity).write_text(hashes, encoding="utf-8")
    names.append(identity)
    if len(names) < 25 or not REQUIRED.issubset(names):
        raise SystemExit(f"FreeCAD GUI assets incomplete: {REQUIRED - set(names)}")
    (bundle / "freecad-manifest.txt").write_text(
        "\n".join(names) + "\n", encoding="utf-8"
    )
    print(f"PASS: packed {len(names)} native GUI workbench/theme files")


if __name__ == "__main__":
    main()
