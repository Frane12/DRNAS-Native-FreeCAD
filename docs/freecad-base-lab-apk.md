# DRNAS FreeCAD Base LAB APK — Android ARM64

Standalone debug-signed **development APK** (`org.drnas.freecad.coreprobe`) distinct from the OCCT-only smoke APK.

Two separate buttons:
- `RUN CAD GEOMETRY TEST`: exercises already device-validated Android OCCT CAD box minus bore.
- `RUN REAL FREECAD C++ TEST`: runs original pinned FreeCAD upstream **Base64.cpp** plus a narrowly adapted **Vector3D.cpp** in a shared Android ARM64 ELF via JNI, including vector math and binary roundtrip.

**This is not all of FreeCADBase, FreeCADApp, a parametric document engine, or a GUI editor.**
Original pinned sources: FreeCAD/FreeCAD `d4ffc7f725973e073907746e88d320d3e438fd34`.
A copy of `Vector3D.cpp` for Android replaces Boost `isnan` with `std::isnan`, removes an unused `Tools.h` include, and includes FCGlobal.h and algorithm. The source adapter generates a diff artifact. Original upstream source is not changed.

The APK workflow reuses the **19 cached OCCT Android ARM64 DSOs**, **CCache FreeCAD source objects**, and **Gradle** dependency cache; verified ELF plus package signatures and SHA-256.
Successful CI **does not prove Android runtime passes**; press both test buttons on a physical arm64 phone and check PASS. No debug APK is claimed as a production build.
