# FreeCAD commands with DRNAS native GLES rendering

Experimental branch: `experiment/freecad-commands-drnas-render-20261024`.
Separate application: **DRNAS FreeCAD MODEL LAB**, `org.drnas.freecad.modelab`.
The existing Coin framebuffer investigation remains on the feature branch.

FreeCAD owns documents, parametric geometry, recompute, save and undo history.
The existing BRep export bridge feeds the native OCCT/GLES viewport. Coin drawing
is disabled in this experiment; FreeCADGui and its ViewProviders still load.
Normal document changes and view visibility notifications refresh the preview.
A document identity poll also handles switching and closing documents.

## Phone test

1. Open the original FreeCAD workspace. Choose **MODEL > Dodaj kocku 20 mm**.
   This creates a real Part Box, creating a document if needed, and selects it.
2. Choose **MODEL > Dimenzije odabrane kocke**. Enter e.g. 30, 20, 15 mm.
3. Choose **MODEL > Rupa kroz odabrano tijelo**. A real Part Cylinder and
   Part Cut are created in an undoable transaction; source objects are hidden.
4. Use **Undo**, then **Redo**. The native preview must follow FreeCAD history.
5. Add another box. Use **Odaberi objekt** to select a named visible BRep
   object in FreeCAD's selection; this is a list selection, not face picking.
6. Verify original FreeCAD edits, visibility changes, document switching and
   closing, save/reopen, rotation, pinch zoom and two-finger pan.

**Fit našeg prikaza** resets the camera. **Bridovi** toggles triangulation lines.
The camera angles and zoom are retained across ordinary geometry refreshes;
geometry normalization still follows the current model bounds.

The hole control is a simple Z-axis through-hole test. It is not a general
face-directed hole tool. There is no native face/edge picking, Sketcher overlay,
selection highlighting or replacement of Coin-dependent editing tools yet.

## Build isolation and verification

A dedicated draft PR targets `feature/full-freecad-android` so its workflow can
restore verified caches from the base branch. The workflow checks out the exact
experimental head SHA, reuses genuine ARM64 native artifacts, and packages a
separate app. It has its own concurrency group and artifact name; no merge or
release publication is required. Native dependency and signature checks remain.

Run `python full-gui/tests/test_model_commands.py` for transaction boundary tests.
These tests do not verify OCCT geometry or Android display output. The Actions
build verifies native compilation and packaging; the phone test verifies rendering.
Look for `MODEL_COMMAND`, `RENDER_BASELINE`, and `MODEL LAB` in diagnostics.
