#!/usr/bin/env python3
"""Reject a signed Android FreeCAD APK if ANY native library has a missing DT_NEEDED.

This catches a real device startup crash missed by previous checks: upstream
libFreeCADGui.so requires Qt6Network/PrintSupport while libFreeCADApp.so needs
Qt6Xml; androiddeployqt only includes app-linked Qt targets by default.
"""
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

DEVICE_LIBS = {
    "libc.so", "libm.so", "libdl.so", "liblog.so", "libz.so",
    "libandroid.so", "libEGL.so", "libGLESv2.so", "libGLESv3.so",
    "libOpenSLES.so", "libjnigraphics.so", "libvulkan.so",
    "libmediandk.so", "libaaudio.so", "libnativewindow.so",
    "libatomic.so", "libstdc++.so", "libc++_shared.so",
}


def inspect(apk):
    if not apk.is_file():
        raise RuntimeError(f"APK does not exist: {apk}")
    missing = []
    with zipfile.ZipFile(apk) as archive:
        libs = {
            name.rsplit("/", 1)[-1]: name
            for name in archive.namelist()
            if name.startswith("lib/arm64-v8a/") and name.endswith(".so")
        }
        if not libs:
            raise RuntimeError("APK has no arm64-v8a native shared objects")
        required = {
            "libCoin.so", "libFreeCADGui.so", "libFreeCADApp.so",
            "Part.so", "PartGui.so", "Sketcher.so", "SketcherGui.so",
            "_PartDesign.so", "PartDesignGui.so",
            "libFreeCADBase.so", "libQt6Network_arm64-v8a.so",
            "libQt6PrintSupport_arm64-v8a.so", "libQt6Xml_arm64-v8a.so",
        }
        for name in sorted(required - libs.keys()):
            missing.append(("APK", name))
        with tempfile.TemporaryDirectory(prefix="drnas-apk-abi-") as tmp:
            for soname, member in sorted(libs.items()):
                target = Path(tmp) / soname
                with target.open("wb") as fp:
                    fp.write(archive.read(member))
                header = subprocess.check_output(
                    ["readelf", "-h", str(target)], text=True
                )
                if not re.search(r"Machine:\s*AArch64", header):
                    raise RuntimeError(f"Wrong ABI in APK library {soname}")
                dynamic = subprocess.check_output(
                    ["readelf", "-d", str(target)], text=True
                )
                if re.search(r"libc\.so\.6|ld-linux|libstdc\+\+\.so\.6", dynamic):
                    raise RuntimeError(f"Linux/glibc dependency in {soname}")
                for dep in re.findall(r"Shared library: \[([^]]+)\]", dynamic):
                    if dep not in libs and dep not in DEVICE_LIBS:
                        missing.append((soname, dep))
    if missing:
        print("ERROR: APK is not linkable at startup; missing DT_NEEDED libraries:")
        for soname, dep in sorted(set(missing)):
            print(f"  {soname} needs {dep}")
        return 1
    print(f"PASS: all {len(libs)} Android ARM64 native libraries have complete DT_NEEDED closure.")
    print("NOTICE: pass is a static check; on-device runtime and FreeCAD workbenches not yet proven.")
    return 0


if __name__ == "__main__":
    sys.exit(inspect(Path(sys.argv[1])))
