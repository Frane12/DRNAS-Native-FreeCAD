#!/usr/bin/env python3
"""Repackage original FreeCAD GUI resources as Android assets with path manifest."""
from pathlib import Path
import shutil
import sys
import os
import hashlib

REQUIRED = {
    "Mod/Part/InitGui.py", "Mod/Sketcher/InitGui.py",
    "Mod/PartDesign/InitGui.py", "Mod/Measure/InitGui.py",
    "data/Gui/Stylesheets/overlay/icons/taskshow.svg",
    "share/Mod/Material/Resources/Materials/Appearance/Default.FCMat",
    "share/Mod/Material/Resources/Materials/Standard/Default.FCMat",
    "share/Mod/Material/Resources/Models/Rendering/BasicRendering.yml",
    "Mod/BIM/ArchMaterial.py", "Mod/BIM/ArchWall.py", "Mod/BIM/ArchWindow.py",
    "Mod/BIM/Arch_rc.py", "Mod/Draft/Draft.py", "Mod/Draft/Draft_rc.py",
    "lib/lazy_loader/lazy_loader.py",
    "Mod/PySide/QtWidgets.py",
}



def defer_measure_init_gui(path: Path):
    """Gate upstream Measure/InitGui.py's eager native imports on Android.

    The original C++ extensions remain packaged unmodified. GUI/Qt may import
    this resource before the first valid Coin3D frame; the Android AUTO MEASURE
    timer imports Measure and MeasureGui separately after the first Coin frame.
    A deliberately explicit environment override can restore original eager
    imports for a controlled A/B test; by default the imports are gated.
    Fail the build if upstream changes the exact import contract.
    """
    original = path.read_text(encoding="utf-8")
    lines = original.splitlines(keepends=True)
    targets = {"import Measure", "import MeasureGui"}
    found = {target: 0 for target in targets}
    for i, line in enumerate(lines):
        statement = line.strip()
        if statement in targets:
            found[statement] += 1
            newline = "\r\n" if line.endswith("\r\n") else "\n"
            if statement == "import Measure":
                lines[i] = (
                    "# DRNAS Android: gate eager native imports until the Coin3D timer\n"
                    "import os as _drnas_measure_os\n"
                    "if _drnas_measure_os.environ.get('DRNAS_MEASURE_INITGUI_EAGER') == '1':\n"
                    "    import Measure\n"
                    "    import MeasureGui\n"
                    "else:\n"
                    "    print('DRNAS_MEASURE_INITGUI DEFERRED: native extensions waiting for Android timer', flush=True)\n"
                    "del _drnas_measure_os\n"
                ).replace("\n", newline)
            else:
                lines[i] = "# DRNAS Android: import MeasureGui moved under guard above" + newline
    if any(count != 1 for count in found.values()):
        raise SystemExit(
            f"Unsafe Measure InitGui change: expected one import each, got {found} in {path}"
        )
    updated = "".join(lines)
    compile(updated, str(path), "exec")
    path.write_text(updated, encoding="utf-8")
    print("PASS: original Measure/InitGui.py eager imports gated "
          "until explicit timer; native binaries retained")



def instrument_draft_initgui(path: Path):
    """Keep upstream Draft logic intact, but preserve its suppressed traceback.

    Draft/InitGui.py catches import exceptions and prints only str(exc).
    On Android that hid which nested module attempted None & int.
    Diagnostic-only change: keep exception handling/return behavior identical.
    """
    original = path.read_text(encoding="utf-8")
    needle = (
        "        except Exception as exc:\\n"
        "            FreeCAD.Console.PrintError(exc)\\n"
    )
    # Python source literals above resolve to a real linefeed on execution.
    needle = needle.replace("\\\\n", "\\n")
    if original.count(needle) != 1:
        raise SystemExit("Draft upstream exception handler changed; refusing unsafe patch")
    replacement = (
        "        except Exception as exc:\\n"
        "            import traceback as _drnas_draft_traceback\\n"
        "            FreeCAD.Console.PrintError(\\\"[DRNAS_DRAFT_IMPORT_TRACEBACK]\\\\n\\\""
        " + _drnas_draft_traceback.format_exc())\\n"
        "            FreeCAD.Console.PrintError(exc)\\n"
    )
    replacement = replacement.replace("\\\\n", "\\n")
    updated = original.replace(needle, replacement, 1)
    compile(updated, str(path), "exec")
    path.write_text(updated, encoding="utf-8")
    print("PASS: Draft InitGui now prints traceback for import failure; no logic changed")


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
    # Original FreeCAD src/Mod/Measure/InitGui.py directly imports Measure and
    # MeasureGui during GUI startup. In QtActivity this can run BEFORE Coin
    # ever presents a frame, bypassing Android's delayed Measure controls.
    defer_measure_init_gui(bundle / "freecad" / "Mod" / "Measure" / "InitGui.py")
    instrument_draft_initgui(bundle / "freecad" / "Mod" / "Draft" / "InitGui.py")
    # Android's native library extractor can ignore Python extension ELF names
    # such as Part.so (no "lib" prefix). Ship the exact relocated binaries in
    # app-private FreeCAD/lib as a reliable CPython and module-loader fallback.
    # Explicitly labeled prebuilt LAB: previous archive predates _PartDesign.so staging.
    extensions = ["Part", "PartGui", "Sketcher", "SketcherGui"]
    if os.environ.get("DRNAS_PARTIAL_GUI_LAB") != "1":
        extensions.extend(["Materials", "MatGui", "_PartDesign", "PartDesignGui", "Measure", "MeasureGui"])
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
    save_runtime = bundle / "freecad" / "lib" / "drnas_android_save.py"
    shutil.copyfile(Path(__file__).with_name("drnas_android_save.py"), save_runtime)
    names.append("lib/drnas_android_save.py")
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
