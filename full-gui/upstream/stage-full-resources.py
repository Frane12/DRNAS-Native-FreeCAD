#!/usr/bin/env python3
"""Stage real upstream FreeCAD Python GUI workbenches, QSS themes and SVG icons."""
from pathlib import Path
import shutil
import sys

EXTENSIONS = {
    ".py", ".ui", ".svg", ".png", ".jpg", ".jpeg", ".gif", ".xpm",
    ".ico", ".qss", ".yaml", ".yml", ".json", ".qm", ".xml", ".csv",
    ".ttf", ".otf", ".txt", ".qml", ".FCStd", ".qrc",
}
SKIP = {"__pycache__", ".git", ".github", "tests", "test", "Tests", "Test"}


def copy_assets(root: Path, destination: Path) -> int:
    if not root.is_dir():
        return 0
    copied = 0
    for file in sorted(root.rglob("*")):
        if not file.is_file() or file.is_symlink() or file.suffix not in EXTENSIONS:
            continue
        relative = file.relative_to(root)
        if any(name.startswith(".") or name in SKIP for name in relative.parts):
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)
        copied += 1
    return copied


def main():
    source, built, destination = map(Path, sys.argv[1:])
    count = 0
    for module in ("Part", "Sketcher", "PartDesign"):
        count += copy_assets(source / "src/Mod" / module, destination / "Mod" / module)
        count += copy_assets(built / "Mod" / module, destination / "Mod" / module)
    count += copy_assets(source / "src/Gui/Stylesheets", destination / "data/Gui/Stylesheets")
    for name in ("PreferencePacks", "PreferencePackTemplates"):
        count += copy_assets(source / "src/Gui" / name, destination / "data/Gui" / name)
    count += copy_assets(built / "data/Gui", destination / "data/Gui")
    for path in (
        "Mod/Part/InitGui.py", "Mod/Sketcher/InitGui.py",
        "Mod/PartDesign/InitGui.py",
        "data/Gui/Stylesheets/overlay/icons/taskshow.svg",
    ):
        if not (destination / path).is_file():
            raise SystemExit(f"Missing original FreeCAD GUI resource: {path}")
    if count < 25:
        raise SystemExit("FreeCAD GUI resource tree is unexpectedly small")
    print(f"PASS: staged {count} upstream resource files and GUI workbenches")


if __name__ == "__main__":
    main()
