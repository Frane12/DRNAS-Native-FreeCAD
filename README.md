## ✅ Android 3D Render LAB v0.0.3 (build success — hardware test pending)

[GitHub Actions run #39563209652](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/39563209652) successfully cross-compiled, linked, signed and packaged **DRNAS FreeCAD Render LAB** for arm64-v8a with the new blue mechanical CAD icon.

📱 **[Download Render LAB debug APK artifact ZIP](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/39563209652/artifacts/12328542985)** — extract the APK from the ZIP. Android package ID is `org.drnas.freecad.coreprobe`; if an older Core LAB debug build is installed, uninstall that old APK first because CI debug signing certificates can differ.

The renderer displays **real OCCT Boolean B-Rep triangulation** (10×20×30 box with cylindrical through-hole) via JNI and Android OpenGL ES 2.0, not a placeholder GL cube. Rotate with one finger, pinch to zoom, pan with two fingers, or reset camera. Previously validated Android native CAD geometry and upstream FreeCAD Base C++ tests are preserved. The output newline display bug is fixed.

**Status: GitHub CI passes** and verifies Android ELF architecture, JNI shared libraries and APK v2 signature. We still need a **physical-device render test** to confirm visible triangles and touch controls. This is a 3D CAD geometry viewer/prototype, *not the complete FreeCAD GUI, FreeCADBase or FreeCADApp*.

OCCT kernel was restored from the existing verified cache (19 libs), not rebuilt. The separate FreeCAD source CCache and Gradle caches remain enabled. See [implementation and limitations](docs/native-render-lab.md).

## Independent FreeCAD C++ Android Core LAB (development branch)

On branch `feature/freecad-base-android`, a real subset of **upstream FreeCAD's own Base64.cpp and Vector3D.cpp** now cross-compiles with Android NDK, alongside the already phone-tested OpenCASCADE kernel. The FreeCAD host C++ tests passed and the Android shared library/PIE ELF audits passed. The **separate Android Core LAB debug APK is successfully built, signed and packed**: [Run #39560534869](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/39560534869), [APK artifact ZIP](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/39560534869/artifacts/12327696324).

App ID `org.drnas.freecad.coreprobe` is deliberately different from the OCCT smoke app; they can coexist. Test **RUN REAL FREECAD C++ TEST** on a phone and capture PASS/FAIL. Native Android execution of the FreeCAD source has **not yet** been verified on a device (unlike the previously confirmed OCCT operations).

**Not a full FreeCADBase or FreeCADApp build.** Compiling 2 upstream C++ files is an initial source-port validation, not a parametric FreeCAD document/Part runtime. The real FreeCADBase/App dependencies (Qt6, ICU, Python/PyCXX, Xerces-C, Boost, etc.) remain. The saved verified OCCT native cache is reused; no expensive OCCT rebuild.

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

### Phase 3: isolated Android CAD core smoke APK
[**Successful native Android APK build #39557289867**](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/39557289867) — includes a **debug-signed APK** with a minimal Android UI, JNI bridge and all Open CASCADE Android ARM64 libraries. [Download build artifact ZIP](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/39557289867/artifacts/12325649914), unzip and install the `DRNAS-FreeCAD-CAD-Smoke-arm64-debug.apk` on an ARM64 Android phone.

The native test automatically models a box and subtracts a cylindrical hole; the screen reports PASS / FAIL, volume and meshing status. **On-device execution is not yet verified** — CI verifies compilation, packaging, ELF architecture and APK signature only. This is **not yet the FreeCAD application**.

The APK workflow restores the previously verified OCCT SDK cache (no kernel recompilation). Its smaller JNI compiler cache and Gradle dependency cache are also enabled.

### Roadmap
1. ✅ Android AArch64 OCCT kernel compile and ELF audit.
2. 🔄 Link and run a real Android geometry/Boolean operation, then build a JNI-driven Android test UI.
3. ⏳ Add OCCT DataExchange (STEP/IGES) as a separately cached configuration.
4. ⏳ Bring up FreeCAD core and required Python/Qt/Coin3D dependencies.
5. ⏳ Build an Android viewport, touchscreen controls, and APK.

Android ARM64 is not generic Linux ARM64: Android uses Bionic and the Android linker, not Linux glibc. Original FreeCAD and OCCT licenses and copyrights remain with their authors.
