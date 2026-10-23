#!/usr/bin/env python3
"""Audit Android FreeCAD Python binding payloads without treating Qt/Coin as bindings."""
from pathlib import Path
import argparse
import sys
import zipfile

BINDINGS = {
    "PySide6": ("PySide6/__init__.py", "PySide6/QtCore", "PySide6/QtWidgets"),
    "shiboken6": ("shiboken6/__init__.py", "shiboken6/Shiboken"),
    "pivy": ("pivy/__init__.py", "pivy/coin.py", "pivy/_coin"),
}
def inspect(names):
    results = {}
    for package, markers in BINDINGS.items():
        matched = {marker: sorted(n for n in names if marker in n)[:3] for marker in markers}
        results[package] = matched
    return results

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("payload", type=Path, help="APK, ZIP or unpacked staging directory")
    parser.add_argument("--require-all", action="store_true", help="Fail CI if bindings are missing")
    args = parser.parse_args()
    if args.payload.is_dir():
        names = [str(p.relative_to(args.payload)).replace("\\", "/") for p in args.payload.rglob("*") if p.is_file()]
    elif zipfile.is_zipfile(args.payload):
        with zipfile.ZipFile(args.payload) as archive:
            names = archive.namelist()
    else:
        parser.error("payload must be a directory, APK or ZIP")
    results = inspect(names)
    missing = []
    for package, markers in results.items():
        ok = all(markers.values())
        print(f"{package}: {'PRESENT (filename check only)' if ok else 'MISSING/INCOMPLETE'}")
        for marker, found in markers.items():
            print(f"  {marker}: {', '.join(found) if found else 'NOT FOUND'}")
        if not ok:
            missing.append(package)
    print("NOTE: filename audit cannot prove ELF ABI, DT_NEEDED closure or Python import success.")
    if missing and args.require_all:
        print("ERROR: required native Python bindings absent: " + ", ".join(missing), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
