# FreeCAD Core port: native source feasibility

## Confirmed on hardware (21 Oct 2026)
Android ARM64 Open CASCADE 7.9.3 in a debug APK ran real geometric solids operations on a phone:
- box 10×20×30 mm minus a cylinder of radius 3 mm and height 30 mm
- B-Rep validity PASS, meshing PASS
- resulting volume **5151.770 mm³** (expected 6000 − 270π).
**This is OCCT, not the FreeCADApp/FreeCADBase shared libraries.**

## Next step: original FreeCAD source
The dedicated `feature/freecad-base-android` branch tests **original** pinned upstream
FreeCAD `src/Base/Base64.cpp` (`d4ffc7f725973e073907746e88d320d3e438fd34`),
plus original `Base64.h` and `FCGlobal.h`, by building a standalone ELF for Android
with NDK r27. No Linux ELF gets relabeled; all Android binaries are compiled using
`aarch64-linux-android26-clang++`. A Linux host executable performs full functional
checks, while Android compile/link/ELF checks establish ABI only (the Android executable
still needs a device run). Original upstream code is downloaded directly from a pinned
commit, not modified or re-licensed here. This is **only a FreeCAD Base source slice**:
success does **not** imply that all of FreeCADBase, FreeCADApp, or FreeCAD's GUI builds.

### Why the full FreeCAD core is harder
FreeCAD's root CMakeLists.txt currently requires Qt6 Core even with `BUILD_GUI=OFF`,
plus ICU, Python/PyCXX, Xerces-C, Zlib, Boost, OCCT, and other integrations.
`src/Base/CMakeLists.txt` builds the `FreeCADBase` shared library, Python wrappers,
and resource generators. `src/App/CMakeLists.txt` builds `FreeCADApp`, links to
FreeCADBase and Qt6 Core/Xml, and compiles document/property/feature modules.
Each dependency must use the **Android/Bionic ARM64** ABI, never generic Linux glibc
ARM64. Host-run generators must be kept separate from Android target binaries.

### Build strategy
1. ✅ Real Android on-device OCCT geometry operations.
2. 🔄 Isolated original FreeCAD C++ source / linker feasibility, with dedicated ccache.
3. ⏳ Build and cache Android Qt6 Core/Xml, ICU, Xerces-C, Boost, embedded Python/PyCXX.
4. ⏳ Configure, compile and verify the actual `FreeCADBase` Android shared library.
5. ⏳ `FreeCADApp` document / Part workbench, Python runtime, STEP and persistence.
6. ⏳ Integrate touch GUI and native FreeCAD Android APK.

Do not recompile OCCT for this source probe. The already verified OCCT install cache
remains available in the main OCCT workflow. CI artifacts are NOT release APKs.
