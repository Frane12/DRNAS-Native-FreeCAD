#!/usr/bin/env python3
"""Stage externally built Android ARM64 Qt-for-Python/Pivy wheels.

No host wheels accepted. This is an integration step, NOT a wheel compiler.
"""
import argparse
import pathlib
import shutil
import struct
import sys
import zipfile

REQUIRED = {"PySide6": ("QtCore", "QtGui", "QtWidgets"),
            "shiboken6": ("shiboken6",), "pivy": ("coin",)}

def arm64(data):
    return len(data) >= 20 and data[:4] == b"\x7fELF" and data[4] == 2 and data[5] == 1 and struct.unpack_from("<H", data, 18)[0] == 183

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wheels", required=True, type=pathlib.Path)
    ap.add_argument("--stage", required=True, type=pathlib.Path)
    args = ap.parse_args()
    wheels = sorted(args.wheels.glob("*.whl"))
    if not wheels:
        ap.error("No Android wheels supplied; refusing host pip fallback")
    args.stage.mkdir(parents=True, exist_ok=True)
    for pkg, modules in REQUIRED.items():
        matching = [w for w in wheels if w.name.lower().startswith(pkg.lower().replace("6", "6") + "-")]
        if len(matching) != 1:
            ap.error(f"Expected exactly one {pkg} wheel, found {len(matching)}")
        wheel = matching[0]
        with zipfile.ZipFile(wheel) as z:
            names = z.namelist()
            if not any("android" in n.lower() or "linux_android" in n.lower() for n in names if n.endswith("WHEEL")):
                metadata = [z.read(n).decode(errors="replace") for n in names if n.endswith(".dist-info/WHEEL")]
                if not metadata or not any("android" in m.lower() for m in metadata):
                    ap.error(f"{wheel.name}: wheel metadata lacks Android platform tag")
            for n in names:
                if n.startswith("/") or ".." in pathlib.PurePosixPath(n).parts:
                    ap.error(f"Unsafe wheel path: {n}")
                if n.endswith(".so") and not arm64(z.read(n)[:20]):
                    ap.error(f"Non-ARM64 native extension: {n}")
            for module in modules:
                if not any(n.startswith(pkg + "/") and pathlib.PurePosixPath(n).name.startswith(module) and n.endswith(".so") for n in names):
                    ap.error(f"{wheel.name}: missing {pkg}/{module} native extension")
            for n in names:
                if n.endswith("/") or ".dist-info/" in n:
                    continue
                dest = args.stage / n
                dest.parent.mkdir(parents=True, exist_ok=True)
                with z.open(n) as source, dest.open("wb") as target:
                    shutil.copyfileobj(source, target)
        print(f"Staged {wheel.name}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
