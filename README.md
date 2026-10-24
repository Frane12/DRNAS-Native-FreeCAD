# DRNAS Native FreeCAD — Android ARM64

**A native Android ARM64 port of FreeCAD, developed by Frane Drnas ([Frane12](https://github.com/Frane12)).**

The project brings FreeCAD to Android phones and tablets as an application running directly on the device. It uses Android ARM64 builds of FreeCAD and its dependencies, without CPU emulation or a remote desktop.

This is an independent community port built on the work of the [FreeCAD](https://www.freecad.org/) community and [Open CASCADE Technology](https://github.com/Open-Cascade-SAS/OCCT). It is currently under active development and is not an official FreeCAD release.

## Current status

As of **24 October 2026**, development has progressed beyond the early geometry-only tests documented below. The developer has reported successful on-device 3D rendering and model interaction on an Android ARM64 phone.

Current development focuses on:

- Integrating and stabilizing the FreeCAD workspace and rendering on Android.
- Improving touch input, camera interaction, zoom, and selection.
- Adapting dialogs, toolbars, and layouts to portrait and landscape screens.
- Packaging the required native libraries and Python dependencies reliably.
- Improving startup, diagnostics, and crash handling.

Working rendering and interaction are development milestones, not a claim that every FreeCAD workbench, file format, or desktop feature is supported. Compatibility across Android devices and graphics drivers still needs broader testing.

## Development and testing

The primary native GUI integration branch is [`feature/full-freecad-android`](https://github.com/Frane12/DRNAS-Native-FreeCAD/tree/feature/full-freecad-android). Other branches contain experiments and earlier milestones.

Build and packaging activity is available in [GitHub Actions](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions). A successful compilation or APK packaging run does not by itself establish that a feature works on a physical device. Experimental artifacts may target different rendering or integration tests.

Public testing is planned. Installation instructions and a clearly identified test build will be documented here when ready.

## Roadmap

1. Stabilize rendering, document interaction, and application startup on physical devices.
2. Refine touch controls and responsive layouts for phones and tablets.
3. Validate workbenches, Python dependencies, and file operations individually.
4. Publish reproducible build instructions and organized public testing guidance.
5. Explore contributing suitable Android compatibility changes upstream and maintaining compatibility with future FreeCAD releases.

These are development goals; no release dates or upstream support commitments are implied.

## Collaboration with the FreeCAD community

Technical feedback from FreeCAD maintainers, Android developers, and testers is welcome. Areas of particular interest include graphics integration, Qt on Android, native dependency builds, and maintainable platform compatibility changes.

The long-term aim is to keep the port aligned with FreeCAD's official release cycle. Potential collaboration could include reviewing focused compatibility patches, sharing technical guidance, and discussing how Android updates could follow future FreeCAD releases. This remains a proposal for discussion, rather than an existing agreement with the FreeCAD project.

For questions or collaboration, [open an issue](https://github.com/Frane12/DRNAS-Native-FreeCAD/issues) or visit [Frane12 on GitHub](https://github.com/Frane12).

## Credits

FreeCAD and its dependencies are the work of their respective authors and communities. Original licenses and copyright notices remain applicable. The FreeCAD name is used to identify the upstream project; this port does not imply endorsement by FreeCAD or the FreeCAD Project Association.

---

## Early build milestones and developer tests

The following notes preserve the initial geometry SDK and smoke-test history. Their “not yet FreeCAD” and “not yet verified” statements describe those individual early artifacts, not the current port as a whole.

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

Android ARM64 uses Bionic and the Android linker rather than desktop Linux glibc.
