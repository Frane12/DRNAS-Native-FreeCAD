#!/usr/bin/env python3
"""Repackage original FreeCAD GUI resources as Android assets with path manifest."""
from pathlib import Path
import shutil
import sys

REQUIRED = {
    "Mod/Part/InitGui.py", "Mod/Sketcher/InitGui.py",
    "Mod/PartDesign/InitGui.py",
    "data/Gui/Stylesheets/overlay/icons/taskshow.svg",
}


def main():
    source, bundle = map(Path, sys.argv[1:])
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
    if len(names) < 25 or not REQUIRED.issubset(names):
        raise SystemExit(f"FreeCAD GUI assets incomplete: {REQUIRED - set(names)}")
    (bundle / "freecad-manifest.txt").write_text(
        "\n".join(names) + "\n", encoding="utf-8"
    )
    print(f"PASS: packed {len(names)} native GUI workbench/theme files")


if __name__ == "__main__":
    main()
