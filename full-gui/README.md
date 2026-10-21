# DRNAS native FreeCAD Android — genuine GUI library integration

## Verified milestones

- **PASS:** pinned upstream FreeCAD `libFreeCADBase.so`, `libFreeCADApp.so`
  and `libFreeCADGui.so` compiled using Android NDK AArch64/Bionic.
- **PASS:** original upstream ARM64 full GUI build:
  [GitHub Actions run 39613337502](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/39613337502).
- **PASS:** separate Qt 6 Android QtActivity / OCCT GUI-LAB APK was already built.
- **NEW:** real `libCoin.so` and other compiled GUI .so libraries are preserved
  in subsequent upstream build artifacts (previous success archived only the three
  FreeCAD .so files, so a new upstream native build is needed).
- **NEW:** `full-gui/integrated-apk/stage-runtime.sh` validates, namespaces ICU,
  stages Android FreeCADGui, Coin3D, real GL4ES/GLU and other native dependency
  libraries for `QT_ANDROID_EXTRA_LIBS`.
- **NEW:** `full-gui/qt-activity/main.cpp` uses Qt's Android QApplication and
  runs a real `dlopen` probe after Qt boots; displays the exact load result.
- **NOT YET PROVED:** actual upstream FreeCAD desktop MainWindow/Sketcher/
  PartDesign/parametric document editing on a phone. The QtActivity test app
  still displays the existing real OCCT interactive CAD viewport, not
  FreeCAD's original workspace. Passing a `dlopen` check is not full GUI.

## Automatic full GUI -> APK pipeline

1. Trigger **DRNAS REAL upstream FreeCADGui Full ARM64 Build** on
   `feature/full-freecad-android`. The new build produces `libCoin.so`
   in the archived native artifact along with FreeCADGui/Base/App.
2. On success, the default-branch
   `.github/workflows/freecadgui-apk-on-upstream-success.yml` triggers
   `DRNAS FreeCADGui QtActivity APK Integration` on the feature branch.
3. Packaging restores the existing feature-branch Qt/OCCT/ICU/Boost/Python
   cache, downloads the verified upstream build artifact, and attempts to
   assemble a signed Android ARM64 APK.
4. The APK job checks the signature, application ID, and the real native
   `libFreeCADGui.so` / `libCoin.so` inside the ZIP. Build artifacts hold the
   APK and logs.
5. A failed *upstream* compilation invokes the existing guarded
   `freecad-instant-autofix.yml` bot. A failed *APK integration* invokes
   `freecad-apk-instant-autofix.yml`, which can attempt up to two safe
   corrections in approved packaging files and dispatch a new APK build.
   Neither bot runs on your phone; both use GitHub's workflow-run events.

Neither workflow deploys to Play Store, publishes a release, or claims
successful real-phone testing. The original standalone FreeCAD/OCCT APK
and the new integration have different Android package identifiers.

## Run manually

- [Full genuine FreeCADGui ARM64](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/workflows/upstream-freecadgui-full-android.yml)
- [Pack native FreeCADGui QtActivity APK](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/workflows/freecadgui-qtactivity-native-apk.yml)
- [Full build AI auto-fixer](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/workflows/freecad-instant-autofix.yml)
- [Native APK AI auto-fixer](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/workflows/freecad-apk-instant-autofix.yml)
