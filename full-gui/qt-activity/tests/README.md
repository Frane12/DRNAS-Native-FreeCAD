# Direct touch regression checks

Build and run with desktop Qt 6 (Widgets and Test):

```sh
cmake -S full-gui/qt-activity/tests -B build/touch-tests
cmake --build build/touch-tests
ctest --test-dir build/touch-tests --output-on-failure
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 build/touch-tests/direct-touch
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 build/touch-tests/portrait-modal
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=3 build/touch-tests/portrait-modal
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 build/touch-tests/surface-lifecycle
python3 full-gui/qt-activity/tests/first-shape.py
```

The harness delivers touchscreen sequences through Qt's platform test interface.
It checks absolute tap coordinates, deferred selection, camera gestures without
selection, the two-to-one finger transition, tool drag release, TouchCancel,
precision selection, modal interruption, ordinary buttons, synthesized mouse
deduplication, persistent multi-selection, and the visible cancel button.

The camera callback is recorded, not executed against FreeCAD. These checks do
not establish Coin rendering correctness or Android device behavior.

## Phone acceptance checks

1. Start in portrait (marker **DRNAS P06**) and create a sphere/cube in the existing Coin view.
2. Tap faces and edges at the touched location. No persistent mouse cursor appears.
3. Drag one finger to orbit; no selection click occurs when lifting it.
4. Pan and pinch with two fingers, then lift one followed by the other. No extra
   click, sketch point, or orbit should occur. Pinch should retain the location
   under the two-finger center.
5. Hold a finger for 500 ms. Move the crosshair above the finger onto an edge,
   then lift to select it. The initial implementation uses a crosshair without
   a magnifier image.
6. Open a sketch or task. One finger uses the tool; two fingers pan/zoom without
   changing the sketch plane. For tools without an edit/task dialog, enable
   **Alat / povlačenje** in **Geste**.
7. Enable **Višestruki odabir**, select two edges, and check that both remain
   selected. Use **Izbornik odabira** for the native context menu.
8. Open/close Tools, a menu, and a modal dialog. Controls must react exactly where
   touched, with no click passed to the model behind them.
9. Interrupt a drag by backgrounding the app, then return. No held click should
   remain. **Odustani** must cancel an active tool with the gesture panel closed.

The app locks both activities to portrait. Model, Properties and Tasks share
bottom dock tabs, with restored overlays disabled and desktop drag title bars
removed. Wide original task forms stay reachable inside scroll areas. The
portrait test includes upstream's `Model` dock, 600-pixel task forms, late
workbench panels, and verifies that the host remains exactly 400 by 869 at
DPR 1, 2 and 3.
Internal dialogs stay on QtActivity's surface while keeping exec/done results;
file pickers and dialogs containing GL widgets retain their platform behavior.
The portrait-modal test covers repeated and nested modal prompts, all close
choices, blocked background commands, actual native-window touch entry, and
toolbar input after dismissal at DPR 1 and 2. Deferred framing fits the first
real shape once per document, without replacing geometry or resetting later
camera changes. Coin/GL4ES and verified ARM64 dependencies remain in use.

The surface lifecycle regression confirms that displaying, hiding and restoring
the QtActivity-like host never suspends ordinary QWidget paint or update events.
Android EGL/QRhi and Coin3D surface recovery still require a physical-device test.
