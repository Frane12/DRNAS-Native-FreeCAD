# DRNAS FreeCAD Render LAB v0.0.4

A **prototype 3D CAD viewport** on Android ARM64 — not full FreeCADApp, Qt GUI, or editable FreeCAD .FCStd documents.

Shapes are real Open CASCADE B-Rep solids generated on the phone and tessellated natively:
- box with an actual Ø6 mm cylindrical through-hole, 10 × 20 × 30 mm
- 20 mm solid cube
- Ø16 mm × 24 mm cylinder
- Ø20 mm sphere

OpenGL ES 2.0 shaded rendering and an optional **triangle-mesh edge overlay** (not exact B-Rep feature edges). Dynamic model bounding box keeps each shape framed. Single finger orbits, pinch zooms, two fingers pan. FreeCAD C++ Base64 + Vector3D source tests remain available.

**Save STL** uses Android's native Storage Access Framework picker; requires no broad storage permissions. Files are binary STL (80-byte header, LE uint32 triangle count, 50 bytes/triangle), using tessellated surface normals and vertices in millimetres. STL is a triangle mesh (suitable for 3D printer slicers), NOT parametric CAD and not STEP. Binary serializer has a CI host-unit test. Geometry validity of every exported shape and file support in external slicers still need real-device verification.

**Build cache:** verified OpenCASCADE Android SDK restored unchanged; FreeCADBase compiler cache and Gradle cache remain enabled. GitHub CI validates compile/signing/ABI only; visible results and STL saving require device test.
