# DRNAS Full FreeCAD Android port — dependency status

## Why the working Render LAB is not yet full FreeCAD
Confirmed on a physical phone: Android arm64-v8a OpenCASCADE Boolean
solid modelling, meshing, an OpenGL ES renderer, touch controls and STL save.
Some genuine FreeCADBase source (`Base64.cpp`, `Vector3D.cpp`) also runs on
Android. This does **not** constitute either complete `FreeCADBase` or
`FreeCADApp` shared libraries, the parametric document engine or the GUI.

## Real upstream source build
Branch `feature/full-freecad-android` attempts actual upstream FreeCAD
CMake from pinned FreeCAD commit
`d4ffc7f725973e073907746e88d320d3e438fd34`.
Target is Android arm64-v8a/Bionic, not Linux glibc AArch64.
Initially `BUILD_GUI=OFF`, retaining both actual `FreeCADBase` and
`FreeCADApp` CMake targets. This is the full core build *attempt*.
Full desktop GUI/Qt Widgets/PySide/Coin3D later needs additional work.
Existing Render LAB remains untouched.

## Current target dependency inventory
| Dependency | Android ARM64 status | Why |
|---|---|---|
| OCCT 7.9.3 modeling subset | **Confirmed / cached** | Native B-Rep solids and meshing |
| Qt 6.8.3 Core/Xml | **New Android SDK bootstrap in CI** | Qt even with GUI OFF, FreeCADApp Qt Core+Xml |
| ICU uc/i18n | **Needed; Android build pending** | Unicode/internationalization |
| Python 3.10+ development/libpython | **Needed; Android build pending** | Embedded FreeCAD Python runtime |
| Xerces-C | **Needed; Android build pending** | XML and persistence |
| Boost program_options/regex/thread/date_time | **Needed; Android build pending** | FreeCAD internals |
| PyCXX, yaml-cpp, zlib etc. | **Dependency inspection pending** | FreeCAD modules |
| OCCT STEP/IGES DataExchange | **Additional OCCT build pending** | STEP import/export; original cached geometry subset lacks it |
| Coin3D/PySide6/GUI | **Later** | Full desktop GUI on Android |

## Build instructions
Actions → **DRNAS FreeCAD Full Core Android ARM64** (the workflow is triggered
by commits to this branch). First job downloads **real Qt6 Android arm64-v8a
and matching Linux host tools**, verifies Android ELF and cross-links
QtCore/QtXml sample, then saves the verified Qt SDK to its own immutable
GitHub cache. Second job restores the existing OCCT cache, Qt cache,
checks out upstream FreeCAD and performs the **real CMake configure and
FreeCADBase/FreeCADApp target compile if dependencies permit**.

An expected dependency failure is a **failure**, not a successful full build;
the CI uploads CMake and compiler logs and retains CCache even if it fails.
Do not claim a FreeCAD Core APK or runnable app until full libraries and Android
device runtime have been independently verified. Source builds can require
multiple dependency rounds.

## Build caching
- OCCT verified SDK restored unchanged (no expensive OCCT rebuild).
- New Qt6 Android SDK cache is saved ONLY after actual arm64 ELF audits and Qt CMake cross-link.
- 2 GB incremental CCache is saved after failures and keyed per run.
- Full build output and logs uploaded even on failure, CMake source pinned by SHA.
- Cached target binaries must be audited as Android AArch64/Bionic before reuse.
