# Direct touch regression checks

Build and run with desktop Qt 6 (Widgets and Test):

```sh
cmake -S full-gui/qt-activity/tests -B build/touch-tests
cmake --build build/touch-tests
ctest --test-dir build/touch-tests --output-on-failure
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 build/touch-tests/direct-touch
```

The harness delivers touchscreen sequences through Qt's platform test interface.
It checks absolute tap coordinates, deferred selection, camera gestures without
selection, the two-to-one finger transition, tool drag release, TouchCancel,
precision selection, modal interruption, ordinary buttons, synthesized mouse
deduplication, persistent multi-selection, and the visible cancel button.

The camera callback is recorded, not executed against FreeCAD. These checks do
not establish Coin rendering correctness or Android device behavior.

## Phone acceptance checks

1. Start in landscape and create a cube in the existing Coin view.
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

This change retains the landscape lock, the existing Qt/Coin/GL4ES rendering
path, and the previously built ARM64 native libraries. Gesture navigation alters
the existing Coin camera; it does not create another GL context or surface.
