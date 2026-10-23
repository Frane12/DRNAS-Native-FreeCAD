# Android New / P03 rendering investigation

Device report: Android 16 ARM64, Qt 6.8.3; user identifies Qualcomm Adreno A840.
GPU identity and driver version are not present in the supplied crash journal.

## Proven boundary

The 2026-10-23 16:24:33 UTC device journal completes App/GUI document and
viewer construction, Coin camera/scene setup, MDI M01-M04, initializeGL,
P01 render-manager reinitialize and P02 activation. M05 setWidget triggers
a synchronous paint. P03 actualRedraw starts and SIGSEGV follows.
No P03 END, M05 END or native stack is available.
Part and PartGui imports and PartWorkbench activation returned successfully
before New in this run. This is not the no-workbench control experiment.

## Source-level refinement

Pinned FreeCAD d4ffc7f725973e073907746e88d320d3e438fd34:
QuarterWidget::paintEvent calls virtual actualRedraw().
The View3DInventorViewer override dispatches by renderType.
Native mode calls renderScene, then GL viewport/clear/depth operations,
then renderGLActionScene. That function calls drawSingleBackground, which
builds an overlay and applies a separate SoGLRenderAction, before the main
Coin SoRenderManager render. Thus P03 does not prove the main Coin scene
was reached, and labeling it a Qualcomm driver fault is premature.

## Compatibility questions

The port links real libGL.so from GL4ES a025615bc79ac43ec911dced8fe5b8eb45a0b72d
with NOEGL=ON and DEFAULT_ES=2. Qt owns the EGL/GLES context.
GL4ES's NOEGL capability probe queries the current context; it does not own
one. Its loader opens the system GLES library. A successful library load
and ES 3.2 Qt snapshot do not establish successful desktop GL translation.

Check, in order:
1. Active render context equals the viewport context during the MDI paint.
2. Bound FBO equals QOpenGLWidget::defaultFramebufferObject; the initial
   shell context's reported depth=0/stencil=0 is not proof of the viewport's
   actual attachments. Qt uses an internal FBO and reparent/resize can change it.
3. Qt GL strings versus directly linked GL strings and actual symbol-library
   origins. Desktop compatibility calls must go through the intended GL4ES
   layer, not an accidental GLES symbol route.
4. Initial GL operations versus background overlay Coin traversal versus
   main Coin traversal. Only after this boundary is known investigate
   capability/function resolution, GL4ES state and shader/compiler behavior
   for the identified Qualcomm driver.

This path uses OpenGL ES through GL4ES; no Vulkan/Turnip backend is selected
by these build flags. Do not replace the vendor driver or apply broad
extension-disable workarounds without a device result.

## New diagnostic

Render trace v3 adds GPU00-GPU05, RV, RS, RA, OV and RM markers. All new
render traces/snapshots are capped to the first two calls per function.
GPU queries record both APIs without consuming glGetError or changing GL state.
The existing blank-viewport fallback remains default.

The Android command toolbar offers a session-only TEST Coin render toggle.
Create one document with rendering disabled, then toggle the test to request
a real redraw; alternatively enable it and press original New once.
Do not activate a workbench for the first control test.
If it crashes, restart and Copy Log. No ADB is required.
A process restart resets the toggle. This is diagnosis, not a runtime fix.

Validation: helper Python syntax and exact-match application to pinned original
sources pass locally. Full ARM64 C++ compilation is performed by GitHub Actions;
GPU behavior requires the physical device.

References:
- https://github.com/FreeCAD/FreeCAD/blob/d4ffc7f725973e073907746e88d320d3e438fd34/src/Gui/View3DInventorViewer.cpp
- https://github.com/ptitSeb/gl4es/blob/a025615bc79ac43ec911dced8fe5b8eb45a0b72d/src/glx/hardext.c
- https://github.com/ptitSeb/gl4es/blob/a025615bc79ac43ec911dced8fe5b8eb45a0b72d/src/gl/loader.c
- https://doc.qt.io/qt-6/qopenglwidget.html
