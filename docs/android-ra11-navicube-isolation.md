# RA11 decoration traversal: device control experiment

Device journal 2026-10-23 17:48:52--17:49:07 UTC, package 39995446015.

## Device-confirmed results

- Startup resolution is effective: QT_SCALE_FACTOR=0.905556, logical 442x960,
  DPR=2.71667; fullscreen succeeds and landscape becomes 960x442.
- Automatic libraries, genuine GUI, Part, PartGui and PartDesign activation return.
- WizardShaft warns that numpy is missing, but PartDesign activation returns code 0
  and activeWorkbench().name() confirms PartDesignWorkbench.
- New returns a real document, Unnamed, and completes N13. All M/W/S/C phases
  complete with Coin drawing temporarily skipped.
- Enabling TEST Coin render reaches the actual scene path.
- Qt reports the current GL context equals the document widget context.
- Both Qt-function and directly linked GL string queries report GL4ES wrapping
  Qualcomm / Adreno (TM) 840 / OpenGL 2.1 wrapper / GLSL 1.20.
  Therefore the previous Qt string probe did not independently query system GLES.
- RS01--RS04 complete, including viewport/clear/depth operations.
- Background overlay OV04, background RA07, main Coin manager RM01, delayed
  annotations RA09 and foreground RA10 all return.
- RA11 decoration traversal begins; SIGSEGV follows without RA11 END.

## Source correlation

Pinned FreeCAD d4ffc7f725973e073907746e88d320d3e438fd34:
decorationroot contains a camera, base color/light model and naviCubeAnnotation.
syncNaviCubeVisibility inserts the NaviCube root only when ShowNaviCube is true.
NaviCube has a synchronization callback that prepares label textures, followed by
SoNaviCube::GLRender. That path constructs overlay geometry, changes Coin
shader/texture/material state, clears scissored depth and renders its retained
scene. The current trace cannot distinguish these internals.

## Minimal control, using the SAME native binaries

Set User parameter:BaseApp/Preferences/View/ShowNaviCube=False after App/Python
initialization but before GUI/view construction. This excludes the navigation
cube from the decoration scene; it does not bypass the main CAD renderer or
RA11 itself. Existing real-render opt-in and previous blank fallback are retained.
The navigation cube is the corner orientation control, not a CAD Box object.
This is a reversible isolation workaround, not a confirmed root-cause fix.

TEST Coin render additionally queries explicit dlsym symbols from system
libGLESv2.so after making the document viewport current. GPU_NATIVE records the
provider, real driver strings, FBO, viewport/depth/stencil, widget size and DPR.
The probe restores a previously different current context. Its measurements
are before redraw, after Qt binds the viewport; they are not measurements at
the exact crash instruction.

Old GPU02 reports wrapper FBO=0 versus QtFBO=1 and a portrait viewport after
rotation. This may be stale GL4ES state; do not blindly bind FBO 0 or change the
Qualcomm driver based on those cached readings.

## Phone test

1. Select the same resolution and start automatic Part Design.
2. New once: expect a real document with the existing diagnostic blank fallback.
3. TEST Coin render once. Inspect whether RA11 and subsequent frame phases return.
4. If stable, test a CAD primitive and Fit All; no claim of working object
   rendering follows from an empty-scene render alone.
5. On failure, relaunch Safe Mode and Copy Log immediately.

No NumPy deployment or native dependency changes are included.
