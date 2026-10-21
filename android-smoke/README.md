# Android GUI smoke APK (NOT FreeCAD)
This isolated, **debug-signed** Android ARM64 APK tests the first real DRNAS Open CASCADE CAD native library on a phone. It is not a FreeCAD port executable or full CAD editor yet.

- Package ID: `org.drnas.freecad.smoke` (does not replace other DRNAS apps).
- No internet/network permission, Android API 26+, ARM64 only.
- Screen contains **RUN CAD GEOMETRY TEST** and **EXIT** buttons.
- On device: native C++ Open CASCADE builds a 10×20×30 mm box, subtracts a radius-3 mm cylindrical through-hole, validates volume/B-Rep, triangulates. Output says PASS or FAIL with an error code.
- C++ native core reuses the **successfully cached OCCT 7.9.3** arm64-v8a SDK; it does **not** recompile the 2,200 OCCT targets.
- CCache for JNI compilation + Gradle dependency cache. Actual Android execution must be confirmed on a phone; a CI PASS only confirms compile/sign/package and ELF linkage.
- GitHub Actions: `.github/workflows/cad-smoke-apk.yml`, download debug APK artifact if successful. A new debug keystore may mean a previous debug APK with the same package must be removed before installing the next one.
