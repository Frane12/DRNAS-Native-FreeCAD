# Device-confirmed Coin3D GLES working baseline: APK 370 (28 October 2026)

**Status: user-confirmed working on Android; preserve as a rollback snapshot.**

- **Immutable code snapshot branch:** `baseline/android-working-370-coin-gles-20261028`.
- **Exact tested source SHA:** `4761661a9248786d8e55a243dbcf53d4828ad58b`.
- **APK integration:** [run #370 / 40761171437](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40761171437), completed **success**.
- **APK artifact:** [DRNAS-FreeCADGui-Real-Native-APK-40761171437-1](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40761171437/artifacts/12848404350); artifact ID `12848404350`; ZIP SHA-256 `702c1af627c46039860fe5f6cadf48d9d67559b8bf127b6e0caf5680f390a100`.
- **Native compile:** [run #154 / 40761171429](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40761171429), completed **success**; native artifact ID `12847973455`; ZIP SHA-256 `5f028aae6a9834a61a5421694b8c5fa7723b5975694f6d82633b5515888739e0`.
- **Native Coin SHA-256 before relocation:** `1d0cd16c27521efe7c6c1aa98e9ba73c2255f7b36b99e20ae978077bb0e2d42a`.
- **Coin SHA-256 in staged and signed APK after safe relocation:** `11712cc1e4ff46b4cfc709568a21efe02b58b43d49b5ddfc8da7202e879b4a3f`.
- **Artifact retention:** GitHub Actions APK artifact expires **2026-11-27 20:25:44 UTC**. An artifact link is **not a permanent APK backup**; preserve a separate downloaded copy before expiration.

## What is confirmed

During the 28 October 2026 phone test the user supplied `tt.txt` (last 256 KiB of a DRNAS FreeCAD session): `EngineBlock` FCStd restored; Coin scene traversals had `afterGL=0x0`; FBO bridge reported `status=0x8cd5`, `error=0x0`; camera arrow Python commands returned `ok=1`. No `0x500` / `GL_INVALID_ENUM` or fatal signal was observed in that captured section. The user explicitly confirmed **"je radi super"** when asked whether the model displayed correctly, including surfaces/colors.

Native Coin guard `drnas_coin_android_gles_guard_v2` was verified in **the signed APK** by the packaging CI. Python/Pivy ABI check verified 6431 old Coin references against the retained newer Coin. Earlier missing guard was traced to `integrate-bindings.py` overwriting the native `libCoin.so` with an outdated bindings bundle copy; this commit preserves the native build.

## Scope and follow-ups

This baseline is confirmed for **the tested FCStd model display, document open, camera arrow actions, and GLES/FBO logs**. It is not a blanket validation of Part Design, Sketcher, all UI pop-ups, other models, or every Save/Save Copy path. The capture is the *last* 256 KiB, not a full startup log. The previous separate save/open, sketch icon and sketch plane picker baselines listed below remain valid for their independent test scopes. Resume Part Design, Sketcher and Qt/touch work from an experiment branch; **do not change the snapshot branch**. Require new phone confirmation before promoting another build.

---

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

## Phone-confirmed Sketcher glyph rendering: 26 October 2026

- Snapshot: `baseline/android-sketcher-icons-working-20261026` at
  `28ffadd82d08f4c22aa9e20d8c0012b45c681a9a`.
- Native run `40405045805`; APK run `40405045867`, artifact `12691412828`.
- User screenshot `222099.jpg` shows red horizontal/vertical and other Sketcher
  constraint glyphs with transparent backgrounds in the actual Coin viewport.
  User asked “to je to druže meštre?” after installing this test build.
- This confirms the raster sampler/blending fix on the phone. It is a separate
  rendering milestone; the earlier 326 Open/Save evidence remains scoped to 326.

## User-designated working build: sketch plane picker (27 October 2026)

- Immutable snapshot branch: `baseline/android-working-sketch-picker-20261026`
  at the exact tested source commit `fbafd4ed6669203fd13a7d7e2ff9b3e7dba08939`.
- Native Android build run: `40413118146`; APK packaging run:
  `40413118153`; [APK ZIP](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40413118153/artifacts/12695950885).
- After testing, Frane said “radi ovo sve dosad ovo prozovi radnim buildom”
  and requested that it be designated a working build. This is a broad device
  report; individual plane-picker steps and an exact crash trace were not supplied.
- The earlier black constraint icons and repeated missing Measure command
  warnings had separately been reported fixed. The camera orientation/view cube
  still crashes on clicking according to the latest device report. The Qt touch
  warning, clipboard notification and verbose Sketcher icon diagnostics were also
  observed; their relationship to the crash has not been established.
- Keep this snapshot unchanged when investigating camera navigation. Previous
  baseline branches and their independently confirmed scope remain preserved.
