# Android mouse, keys and window recovery

Packaging-only change on the existing ARM64 FreeCAD runtime. The startup resolution picker, automatic PartDesign, floating Tools, opt-in Coin rendering and NaviCube isolation remain.

Open Miš above LAB. Move the relative touchpad to place the crosshair; LMB/MMB/RMB send Qt mouse events. Enable Drži, tap a mouse button, move on the pad and tap the button again to release. Ctrl/Shift/Alt latch until released. Otpusti releases everything. Zoom uses a spring slider or +/- wheel steps; Fit calls the real active FreeCAD view. The panel position and sensitivity are saved. No new native or OpenGL window is created.

Closing the panel, rotating, hiding the host or deactivating the app releases held input. Keyboard input uses CAD focus and respects modal dialogs. Mouse coordinates stay in logical Qt coordinates, without multiplying by device pixel ratio.

A deferred geometry guard brings visible MDI subwindows and owned dialogs inside the host after show/move/resize. Vrati prozore additionally maximizes the active document. This does not rebuild or reparent a GL surface. Check on the device in portrait and landscape, at two startup scales, including the material error dialog and a held MMB drag during rotation.

The reported content:// import and Material not found errors are separate unresolved resource/import issues. A successful package build is not physical-device input validation or confirmation that the Adreno/Coin rendering crash is fixed.
