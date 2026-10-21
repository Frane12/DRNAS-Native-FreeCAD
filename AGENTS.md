# DRNAS FreeCAD — GitHub build agent instructions

This repository builds **real FreeCAD** for Android ARM64/Bionic. Preserve
the distinction between compiling `libFreeCADGui.so`, packaging the library
in a QtActivity APK, and successfully running the upstream FreeCAD workspace
on a real Android device. Never label one milestone as the other.

## Automated agent installed

- `.github/workflows/freecad-instant-autofix.yml` observes failed **upstream FreeCADGui ARM64 builds**.
- `scripts/freecad-ai-autofix.py` calls OpenAI with the `OPENAI_API_KEY` Actions secret.
- Repairs are limited to a few build-helper scripts, at most two in a row.
  Never edit or echo the secret; never disable ELF/ABI checks or fake build success.
- `.github/workflows/freecadgui-apk-on-upstream-success.yml` dispatches the
  Android packaging job after a successful genuine upstream GUI build.
- `.github/workflows/freecadgui-qtactivity-native-apk.yml` runs that packaging
  in the feature branch (required to reuse branch-scoped Qt/OCCT/ICU caches).
- `.github/workflows/freecad-apk-instant-autofix.yml` and
  `scripts/freecad-apk-ai-autofix.py` provide a second guarded AI repair agent
  for failed QtActivity APK integrations. It skips missing upstream binaries
  and limits changes to a few existing packaging sources.

## Native GUI integration

- Primary source branch: `feature/full-freecad-android`.
- Real compiler workflow: `.github/workflows/upstream-freecadgui-full-android.yml`.
- Android packaging helper: `full-gui/integrated-apk/stage-runtime.sh`.
- Qt6 Android entrypoint: `full-gui/qt-activity/main.cpp` with its CMake project.
- The runtime probe dlopens FreeCADBase, FreeCADApp, Coin3D, GL4ES and
  FreeCADGui after QApplication exists. It must **not** call upstream
  `Gui::Application::runApplication()` inside QtActivity because that routine
  constructs its own QApplication.
- Real upstream MainWindow, FreeCAD Python workbenches, and document editing
  need separate initialization and physical-device validation.
- Keep all ARM64 Bionic libraries from authentic Android builds. Do not bring
  x86_64/glibc desktop libraries into Android packages.
- Keep previously working CAD/OCCT APK separate; distinct Android package IDs.

## When a GitHub Action fails

Inspect the first actual error in job logs, record the failing target/file,
validate a minimal fix, and preserve ccache and native artifacts across failures.
Do not delete completed build artifacts or spend time recompiling Qt/OCCT SDKs
when verified GitHub cache keys are available. Do not push broken release APKs.
