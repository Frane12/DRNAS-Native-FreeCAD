#!/usr/bin/env python3
"""Validate real Android ARM64 Python extension payloads before APK packaging.

Expected layout:
  <stage>/python-compat/PySide/*.py
  <stage>/python-bindings/PySide6/...
  <stage>/python-bindings/shiboken6/...
  <stage>/python-bindings/pivy/...
No host Linux extension is accepted as an Android extension.
"""
import argparse
from pathlib import Path
import re
import struct
import sys

REQUIRED = {
    "PySide6": ("QtCore", "QtGui", "QtWidgets"),
    "shiboken6": ("shiboken6",),
    "pivy": ("coin",),
}
ELF_MAGIC = b"\x7fELF"
AARCH64 = 183

def elf_arm64(path):
    with path.open("rb") as stream:
        header = stream.read(20)
    return (len(header) >= 20 and header[:4] == ELF_MAGIC
            and header[4] == 2 and header[5] == 1
            and struct.unpack_from("<H", header, 18)[0] == AARCH64)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("stage", type=Path)
    p.add_argument("--strict", action="store_true")
    args = p.parse_args()
    root = args.stage / "python-bindings"
    errors = []
    for package, modules in REQUIRED.items():
        directory = root / package
        for module in modules:
            candidates = list(directory.glob(module + "*.so"))
            if not candidates:
                errors.append(f"{package}/{module}: missing native .so")
            for candidate in candidates:
                if not elf_arm64(candidate):
                    errors.append(f"{candidate}: not ELF64 little-endian AArch64")
                if re.search(r"cpython-(?!314(?:-|_))\d+", candidate.name):
                    errors.append(f"{candidate}: extension filename indicates non-3.14 Python ABI")
        init = directory / "__init__.py"
        if not init.exists():
            errors.append(f"{package}: missing __init__.py")
    compat = args.stage / "python-compat/PySide/__init__.py"
    if not compat.is_file():
        errors.append("PySide legacy facade missing")
    if errors:
        for error in errors:
            print("BINDING AUDIT: " + error, file=sys.stderr)
        if args.strict:
            return 1
    else:
        print("BINDING AUDIT: expected native extension names and ARM64 ELF headers present")
    print("NOTE: does not prove importability, DT_NEEDED closure, Python ABI or Qt ABI compatibility")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
