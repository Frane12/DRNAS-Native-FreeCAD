#!/usr/bin/env python3
"""Add original, pinned FreeCAD material libraries to an existing GUI artifact."""
from pathlib import Path
import shutil
import sys

DEFAULT_APPEARANCE = "5dbb7be6-8b63-479b-ab4c-87be02ead973"
BASIC_RENDERING = "f006c7e4-35b7-43d5-bbf9-c5d572309e6e"


def copy_tree(source, target):
    if not source.is_dir():
        raise SystemExit(f"Missing upstream resource directory: {source}")
    count = 0
    for file in sorted(source.rglob("*")):
        if file.is_file() and not file.is_symlink():
            relative = file.relative_to(source)
            if any(part.startswith(".") or part == "__pycache__" for part in relative.parts):
                continue
            destination = target / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, destination)
            count += 1
    return count


def stage(source, destination):
    material = source / "src/Mod/Material"
    resources = material / "Resources"
    # Match App.getResourceDir() == <FreeCAD home>/share/ in this Android build.
    # The module itself remains under <home>/Mod, as required by Python startup.
    count = copy_tree(resources, destination / "share/Mod/Material/Resources")
    for file in sorted(material.glob("*.py")):
        if file.name.startswith("Test"):
            continue
        target = destination / "Mod/Material" / file.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)
        count += 1
    count += copy_tree(material / "materialtools", destination / "Mod/Material/materialtools")
    # InitGui scripts resolve workbench icons via the same ResourceDir.
    for module in ("Material", "Part", "Sketcher", "PartDesign"):
        count += copy_tree(source / "src/Mod" / module / "Gui/Resources",
                           destination / "share/Mod" / module / "Resources")
    root = destination / "share/Mod/Material/Resources"
    required = {
        "Materials/Appearance/Default.FCMat": DEFAULT_APPEARANCE,
        "Models/Rendering/BasicRendering.yml": BASIC_RENDERING,
    }
    for relative, uuid in required.items():
        file = root / relative
        if not file.is_file() or uuid not in file.read_text(encoding="utf-8"):
            raise SystemExit(f"Missing or mismatched upstream material UUID: {relative}")
    if not (root / "Materials/Standard/Default.FCMat").is_file():
        raise SystemExit("Missing default physical material")
    if not (root / "icons/MaterialWorkbench.svg").is_file():
        raise SystemExit("Missing original Material workbench icon")
    print(f"PASS: staged {count} original material/model/icon resources; default UUIDs verified")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: stage-material-resources.py pinned-FreeCAD-source resource-dir")
    stage(*map(Path, sys.argv[1:]))
