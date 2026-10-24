#!/usr/bin/env python3
"""Fail-closed Android ARM64 NumPy wheel preflight and safe extraction."""
import sys, zipfile
from pathlib import Path
src, dst = map(Path, sys.argv[1:])
wheels = list(src.glob("numpy-*.whl"))
if len(wheels) != 1:
    raise SystemExit(f"Expected exactly one NumPy wheel: {wheels}")
wheel = wheels[0]
if not ("cp314" in wheel.name and "android" in wheel.name and "arm64" in wheel.name):
    raise SystemExit(f"Wrong Python/Android ABI: {wheel.name}")
dst.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(wheel) as z:
    members = z.infolist()
    natives = [m for m in members if m.filename.startswith("numpy/") and m.filename.endswith(".so")]
    if not natives or not any("_multiarray_umath" in m.filename for m in natives):
        raise SystemExit("NumPy core ARM64 extension missing")
    for member in members:
        name = member.filename
        path = Path(name)
        if path.is_absolute() or ".." in path.parts:
            raise SystemExit(f"Unsafe wheel entry: {name}")
        if not (name.startswith("numpy/") or name.startswith("numpy.libs/") or ".dist-info/" in name):
            raise SystemExit(f"Unexpected wheel payload: {name}")
        if name.endswith(".so"):
            binary = z.read(member)
            if binary[:4] != b"\x7fELF" or int.from_bytes(binary[18:20], "little") != 183:
                raise SystemExit(f"Non-AArch64 extension: {name}")
        if member.is_dir():
            (dst / path).mkdir(parents=True, exist_ok=True)
        else:
            target = dst / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(z.read(member))
print(f"PASS: staged {wheel.name} with {len(natives)} native extensions")
