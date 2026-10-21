# Native FreeCAD GUI integration — not a desktop GUI mock

The real upstream FreeCADBase + FreeCADApp ARM64 libraries pass Bionic
dlopen on the user's Android phone. The 3D CAD viewport (Open CASCADE/
OpenGL ES), gestures, camera controls and STL export have been tested.

**Upstream FreeCADGui is not compiled or running yet.** This task aims
to port it rather than falsely label an Android view as full FreeCAD.

## First compile milestone

Reuse the known-good Qt 6.8.3 Android QtBase/QtTools cache; add the QtSvg
module without rebuilding other SDK libraries. Cross-link a real
QMainWindow + QOpenGLWidget + QDockWidget + QTreeWidget + QtSvgRenderer
for Android Bionic arm64. The resulting library has a function to create
a Qt GUI window, but that function must ONLY be called after QtActivity
has initialized QApplication: compiling or dlopen alone is not enough.

## Remaining upstream GUI dependencies

* Android QtActivity platform bootstrapping, runtime plugins and assets.
* Coin3D/Pivy as actual Android ARM64 libraries with GPU paths.
* OpenGL desktop/GLU compatibility with Android OpenGL ES; FreeCADGui
  cannot be made to work just by setting BUILD_GUI=ON.
* PySide6/Shiboken6 compiled for the same Android CPython and Qt.
* Actual FreeCAD Document <-> ViewProvider <-> Sketcher/PartDesign
  connectivity and real .FCStd document opening and saving.

The known working OCCT viewport APK is deliberately not overwritten;
runtime testing of a Qt GUI waits for its matching Android bootstrap.
