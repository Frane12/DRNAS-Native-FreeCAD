# Android storage and command diagnostics

Source review: upstream FreeCAD commit d4ffc7f725973e073907746e88d320d3e438fd34.

- `src/Gui/CommandDoc.cpp`, `StdCmdNew::activated`: invokes `App.newDocument()`, then sets view orientation. It does not save an FCStd file.
- `src/App/Application.cpp`, `Application::newDocument`: constructs `Document` before emitting `signalNewDocument` to GUI observers.
- `src/App/Document.cpp`: constructor touches Uid; `onChanged` calls `Base::FileInfo::createDirectories` for `TransientDir`. `getTransientDirectoryName` uses `Application::getUserCachePath`, UUID, hash and instance ID. New therefore accesses disk before the GUI view exists.
- Opening FCStd in `Application::openDocument` also calls `newDocument` before restoring data. `Document::restore` uses `Base::ifstream`, zipios and XMLReader. This is a real filesystem path pipeline, not an Android content-provider URI reader.
- `src/Base/FileInfo.cpp` uses `std::filesystem`; the built Android/Bionic C++ runtime provides it. ABI checks remain mandatory. A desktop `content://` string cannot be passed as a filesystem filename. The external document picker/import-copy boundary still needs a device test and is not claimed fixed here.
- `src/App/ApplicationDirectories.cpp` accepts `FREECAD_USER_HOME`, `FREECAD_USER_DATA`, `FREECAD_USER_TEMP`. Existing private directories are now supplied before App initialization, with TMPDIR for standard C/C++ temporary-file operations.
- `src/Mod/Part/InitGui.py` imports top-level BasicShapes, CompoundTools and BOPTools. Adding only Mod does not resolve packages below Mod/Part. InitGui catches import failures internally, so activation returning zero does not prove command completeness. Module paths are supplied before GUI initialization; original command imports and their full tracebacks are audited after activation. Real PySide/shiboken/pivy availability is logged; no substitute Qt bindings or commands are introduced.

Packaging now changes its extraction manifest identity when asset contents change, including same-name native libraries. Previously identical filename lists could leave old extracted payloads after APK updates.

Device test order: Storage, New no view, Part (copy command errors), New. `New no view` creates and closes a real document with `hidden=True`; GUI observers may still run, but the requested 3D view is suppressed. It does not claim to isolate all GUI code. Storage checks actual FreeCAD cache/temp mkdir, write, fsync, rename, read and removal using disposable probe files.

Host validation: packaging content-change regression, storage read/write/cleanup and error-journaling tests. APK compilation and ARM64 dependency checks run in GitHub Actions. Physical Android execution remains unverified until the user tests it.
