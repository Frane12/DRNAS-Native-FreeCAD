# DRNAS Native FreeCAD — Android ARM64

Independent **Android porting effort**, based on upstream [FreeCAD](https://github.com/FreeCAD/FreeCAD) and [Open CASCADE Technology](https://github.com/Open-Cascade-SAS/OCCT).

### Verified phase 1: OCCT 7.9.3 Android geometry SDK
[Successful GitHub Actions run #39553822111 (attempt 3)](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/39553822111) cross-compiled **19 AArch64 Android ELF shared libraries**, plus SDK headers. Android NDK PIE check passed, ELF ABI audit passed, SHA-256 manifest produced. The result is an **OCCT CAD kernel**, **not yet a compiled FreeCAD application or GUI**.

Build details: Linux x86_64 cross-compiles via Android NDK 27.2, Android API 26, arm64-v8a, Bionic and /system/bin/linker64. 2 GB **ccache** and a separate **verified installed-OCCT cache** are enabled; both were saved successfully.

### Phase 2: Android CAD geometry smoke harness
[Android geometry smoke workflow](.github/workflows/geometry-android-arm64.yml) restores the already verified OCCT SDK from **cache only**, then cross-links a tiny standalone Android bridge (`libdrnas_occt_geometry.so`) and CLI (`drnas-geometry-check`). It models a 10×20×30 box, subtracts a cylindrical through-hole, validates B-Rep, checks analytic volume, and triangulates the result. Its CI verifies compilation and ELF dependencies only; **the geometry operations themselves require execution on an Android device**. Phase 2 is not FreeCAD Core and not an APK.

After a successful phase-2 build, download the artifact, extract it and send the `geometry-arm64` directory to an Android phone with adb:
```sh
adb push geometry-arm64 /data/local/tmp/drnas-freecad
adb shell chmod 755 /data/local/tmp/drnas-freecad/drnas-geometry-check
adb shell 'LD_LIBRARY_PATH=/data/local/tmp/drnas-freecad/lib /data/local/tmp/drnas-freecad/drnas-geometry-check'
```
Expected success output: `DRNAS OCCT ARM64 geometry PASS, box-minus-bore volume=... mm3`.
This is a developer smoke test, not an end-user Android installation.

### Roadmap
1. ✅ Android AArch64 OCCT kernel compile and ELF audit.
2. 🔄 Link and run a real Android geometry/Boolean operation, then build a JNI-driven Android test UI.
3. ⏳ Add OCCT DataExchange (STEP/IGES) as a separately cached configuration.
4. ⏳ Bring up FreeCAD core and required Python/Qt/Coin3D dependencies.
5. ⏳ Build an Android viewport, touchscreen controls, and APK.

Android ARM64 is not generic Linux ARM64: Android uses Bionic and the Android linker, not Linux glibc. Original FreeCAD and OCCT licenses and copyrights remain with their authors.
