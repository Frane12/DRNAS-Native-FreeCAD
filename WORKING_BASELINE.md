# Device-confirmed Android baseline: APK 326

On 26 October 2026 Frane explicitly confirmed both Save and reopening the
saved `.FCStd` on his Android phone. Asked whether both steps succeeded, he
answered “sveeere”. This is user-reported device evidence, following the
successful native build and APK checks; no new post-fix device log was supplied.

## Exact tested build

- Snapshot branch: `baseline/android-working-326` (retain the exact tested commit).
- APK integration and native source commit: `8a5b799fc5081fcd322448cc06962c8aaca783ed`.
- APK integration run: [326 / 40330149643](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40330149643).
- [APK artifact ZIP](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40330149643/artifacts/12654457741).
- APK artifact ZIP SHA-256: `9762530d6e26beaf4c9d48e21af361e7aa251c1a5f4076f0b6126d83c2dc1344`.
- Native libraries: successful [native run 119 / 40329326848](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40329326848).
- Native artifact ID: `12654751633`; ZIP SHA-256: `af0847bc385f530038358015078671c65195db6869100e9107af9f66fa8cf83a`.
- Upstream FreeCAD revision: `d4ffc7f725973e073907746e88d320d3e438fd34`.

## Confirmed scope

Standard Open, actual FCStd/BRep rendering, Save and reopening a saved FCStd
are confirmed on the user's phone. CI also passed 15 Android host regression
tests, Java file transfer tests, native ABI checks and APK v2/v3 signing checks.
This does not establish every workbench, BIM parametric editing, every document
type, every SAF provider, Save As/Save Copy edge case, or clean application exit.

## Preserve these paths

Continue development on `feature/full-freecad-android`. Retain both baseline
snapshots; do not move them when changing the renderer, input, file handling
or dependencies. A later build needs its own device check before replacing
this baseline.

Open uses Android SAF and an app-private staged copy. Keep file-backed BRep
restore (`DirectAccess=False`) and lazy registration of the bundled BIM/Draft
module directories. Bind the standard Open action after delayed creation and
across workbench rebuilds, retaining its original icon and menu placement.

Save first creates and validates a full native FCStd in durable app-private
storage, then copies it through an Android SAF content URI. A first Save or
Save As selects a destination; subsequent Save for the same open document
reuses it. Save Copy selects a destination and preserves modified state.
Cancellation and transfer failure retain the local copy and dirty state;
success is acknowledged only after the output stream flushes and closes.
Concurrent edits remain marked modified. Open imports do not silently overwrite
the original provider file.

Native geometry serialization uses OCCT's filename writer inside the normal
FCStd writer, matching `TopoShape::exportBrep(filename)`: format 1, triangulation
and normals off. Document properties, object data, element maps and GUI state
remain in the FCStd archive. Optional preview thumbnails remain disabled.
Packaging requires `drnas_android_save_trace_v1` in FreeCADApp and
`drnas_android_brep_filename_save_v1` in Part.so to reject stale native inputs.
Android-only follow-up builds must select native run 119 or a compatible newer
build; the old native-115 fallback lacks these required symbols.

## Save diagnosis leading to this baseline

`save.txt` (APK 318) aborted inside `doc.saveCopy()` before the Android picker.
Disabling thumbnail generation did not resolve it: `nece.txt` still aborted
with `SaveThumbnail=False`, while a separate BRep export worked.

The native checkpoints in APK 323 and `padedole.txt` located the abort inside
the copied `BRepTools_Write` helper, writing `Sphere.Shape.brp`. XML and ZIP
entry creation completed; the last marker was `SAVE_BREP 54 ... BEGIN`.
That helper creates a FreeCAD-owned ofstream and passes it into OCCT's
ShapeSet writer. The Android fix uses OCCT's own filename writer, as the
working BRep export does. The exact instruction inside the old helper that
aborted is not established. APK 326's successful Save/Open device round trip
confirms the replacement path works for the user's test.

## Retained earlier Open baseline: APK 316

- Snapshot: `baseline/android-working-316` at `c0ebc95a6ce78d84b80eb81728359b230774c840`.
- [APK run 316 / 40311394406](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40311394406).
- [Earlier APK ZIP](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40311394406/artifacts/12645043643).
- Artifact ZIP SHA-256: `7701c123af6665179f72b5be7c8fa92d2497c3646e93994ba95623451d72ba17`.
- Native run 115 / `40309325619`, source `ee342e9b976b31e4b9601aac67554b8cbe8928b4`.

`idemo.txt` confirmed standard Open -> SAF -> BREP_TEMP -> actual document
restore and framebuffer PASS on the phone. It established Open/rendering;
Save was not working in that earlier baseline.
