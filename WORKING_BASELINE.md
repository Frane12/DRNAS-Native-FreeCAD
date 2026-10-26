# Device-confirmed Android baseline: APK 316

Frane confirmed this diagnostic build as the working base on 26 October 2026.
The supplied `idemo.txt` device log confirms that the standard Open icon enters
the Android SAF picker and completes real FCStd/BRep restore.

## Exact tested build

- Snapshot branch: `baseline/android-working-316` (retain the tested commit).
- APK integration commit: `c0ebc95a6ce78d84b80eb81728359b230774c840`.
- APK integration run: [316 / 40311394406](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40311394406).
- [APK artifact ZIP](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40311394406/artifacts/12645043643).
- Artifact SHA-256: `7701c123af6665179f72b5be7c8fa92d2497c3646e93994ba95623451d72ba17`.
- Native libraries: successful [native run 115 / 40309325619](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/runs/40309325619), source `ee342e9b976b31e4b9601aac67554b8cbe8928b4`.
- Upstream FreeCAD revision: `d4ffc7f725973e073907746e88d320d3e438fd34`.

## Device evidence and scope

The log contains `ANDROID_OPEN DEFAULT DirectAccess=False`,
`ANDROID_OPEN Std_Open ROUTED count=1`,
`ANDROID_OPEN ICON_REQUEST -> SAF_PICKER`,
`ANDROID_OPEN PICKER_READY -> BREP_TEMP`,
`ANDROID_OPEN RETURN document=...`, and `TEMP_BRIDGE_RETURN ok=1`.
Subsequent Coin/Qt framebuffer diagnostics report PASS. Reopening the staged
copy also succeeds. Draft/Arch module specifications resolve to bundled
scripts; full BIM parametric editing is not established by this log.

This establishes real document opening and rendering on the user's phone.
It does not establish every workbench, save/export, or clean application exit.
The log records a Save press without a save-success marker and later reports
an unclean prior session; it does not contain a fatal Save stack trace.

## Subsequent work

Continue from `feature/full-freecad-android`, which incorporates this source.
Keep the snapshot branch as the exact comparison point. The promotion retains
the runtime and native build sources from the tested commit, while preserving
the feature branch's existing Measure regression step and R264 LAB workflow.

Keep file-backed BRep restore as the Android default, and bind Open after
delayed QAction creation and across workbench rebuilds. Preserve this baseline
before experimenting with Save or renderer/input changes. Require a device
check before calling another APK a confirmed replacement.

## Save follow-up

Open currently imports an app-private cached copy, not a writable reference to
the provider's original file. Native Save targets that local filename.
Android saving needs a separate write path: create a complete FCStd in durable
private storage, let Android select a destination through SAF, then stream the
completed file through its content URI. Retain the local copy on cancellation
or transfer failure. Remember the granted destination for subsequent Save;
Save As selects a new destination. Report success only after the destination
write completes.

The integration now implements this handshake for `Std_Save`, `Std_SaveAs`,
and `Std_SaveCopy`. The first Save chooses a new Android destination; later
Save operations on the same open document reuse it. A copy operation always
chooses a destination and preserves the document's modified state. Existing
Open sources are read-only imports; the original provider file is not silently
overwritten. Native serialization uses `saveCopy` with `DirectAccess=False`,
validates the FCStd archive and retains it under private `drnas-documents`.
Provider writing is asynchronous, with no success acknowledgement until the
output stream flushes and closes. Concurrent edits remain marked modified.

Host tests cover the handshake and transfer failures. This Save implementation
still requires a device round-trip check; APK 316 remains the confirmed Open
baseline until the user confirms the new build.
