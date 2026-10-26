#!/usr/bin/env python3
"""Consolidate NumPy vendored libc++ with the verified QtActivity Android runtime.

Diagnostic branch only. The ABI must match exactly. Rewrite existing .dynstr
DT_NEEDED name IN PLACE; patchelf would risk changing PT_LOAD/Qt EGL layout.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys


def readelf(path, *args):
    return subprocess.check_output(["readelf", "-W", *args, str(path)], text=True)


def needed(path):
    return re.findall(r"\(NEEDED\).*?Shared library: \[(.*?)\]", readelf(path, "-d"))


def soname(path):
    match = re.search(r"\(SONAME\).*?Library soname: \[(.*?)\]", readelf(path, "-d"))
    return match.group(1) if match else None


def symbols(path):
    result = {}
    for line in readelf(path, "--dyn-syms").splitlines():
        parts = line.split()
        if (len(parts) >= 8 and parts[6] != "UND" and
                parts[3] in ("FUNC", "OBJECT", "NOTYPE") and
                parts[4] in ("GLOBAL", "WEAK")):
            key, abi = parts[7], (parts[2], parts[3], parts[4], parts[5])
            if key in result and result[key] != abi:
                raise RuntimeError(f"Inconsistent symbol {key} in {path}")
            result[key] = abi
    if len(result) < 1000:
        raise RuntimeError(f"Invalid libc++ exports: {path} has {len(result)}")
    return result


def require_arm64(path):
    header = readelf(path, "-h")
    if "AArch64" not in header or "ELF64" not in header:
        raise RuntimeError(f"Not Android ARM64 ELF: {path}")


def consolidate(primary, mod, report):
    require_arm64(primary)
    if soname(primary) != "libc++_shared.so":
        raise RuntimeError("Wrong QtActivity libc++ SONAME")
    copies = sorted((mod / "numpy.libs").glob("libc++_shared-*.so"))
    if len(copies) != 1:
        raise RuntimeError(f"Expected one NumPy libc++: {copies}")
    private = copies[0]
    require_arm64(private)
    if soname(private) != private.name:
        raise RuntimeError(f"Unexpected NumPy libc++ SONAME: {private}")
    if set(needed(primary)) != set(needed(private)):
        raise RuntimeError("The two libc++ copies have different system dependencies")
    left, right = symbols(primary), symbols(private)
    if left != right:
        raise RuntimeError("libc++ ABI mismatch: exported names/types/sizes/visibility differ")
    affected = sorted(p for p in mod.rglob("*.so") if private.name in needed(p))
    if (len(affected) != 2 or
            not any("_multiarray_umath." in p.name for p in affected) or
            not any("_pocketfft_umath." in p.name for p in affected)):
        raise RuntimeError(f"Unexpected consumers of private libc++: {affected}")
    old = private.name.encode() + b"\x00"
    new = b"libc++_shared.so\x00"
    if len(new) > len(old):
        raise RuntimeError("Replacement does not fit original dynstr")
    before = {}
    for path in affected:
        blob = path.read_bytes()
        if blob.count(old) != 1:
            raise RuntimeError(f"Expected one old DT_NEEDED string: {path}")
        before[str(path.relative_to(mod))] = hashlib.sha256(blob).hexdigest()
        changed = blob.replace(old, new + b"\x00" * (len(old) - len(new)), 1)
        if len(changed) != len(blob):
            raise RuntimeError("ELF segment layout modified unexpectedly")
        path.write_bytes(changed)
        if private.name in needed(path) or "libc++_shared.so" not in needed(path):
            raise RuntimeError(f"New DT_NEEDED did not validate: {path}")
    private.unlink()
    results = {
        "status": "PASS", "matching_exported_symbols": len(left),
        "method": "in-place ELF dynstr replacement (preserves PT_LOAD)",
        "main_soname": "libc++_shared.so", "removed_private_soname": private.name,
        "affected_modules": [str(p.relative_to(mod)) for p in affected],
        "original_sha256": before,
        "qt_libcxx_sha256": hashlib.sha256(primary.read_bytes()).hexdigest(),
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print("PASS: consolidated NumPy libc++ ABI:",
          len(left), "matching symbols,", len(affected), "modules relinked")
    return results


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("Usage: consolidate-numpy-cxx.py <qt libc++.so> <FreeCAD Mod> <report.json>")
    try:
        consolidate(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))
    except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
        raise SystemExit(f"FAIL-CLOSED NumPy libc++ consolidation: {exc}") from exc
