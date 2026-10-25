# Android P07: Coin registry, system insets and Qt selection handles

Device report: objects exist in the model tree but the viewport shows only
its gradient. Camera gestures repeatedly fail at `<string>` line 5 with
`RuntimeError: No SWIG wrapped library loaded`. Android's clock overlays the
top command row and its navigation buttons overlay the right dock. Text
selection also reports a null `CursorView` in `CursorHandle.width()`.

## Verified source findings

* `DrnasTouchInput::flushCamera()` called `getCameraNode()` without importing
  `pivy.coin`. The runtime only used `find_spec('pivy')`, which does not load
  its native extension or register SWIG types.
* Pinned upstream `src/Gui/View3DPy.cpp::getCameraNode()` calls
  `Base::Interpreter().createSWIGPointerObj()`. `src/Base/swigpyrun.inl`
  returns failure when `SWIG_GetModule(nullptr)` finds no registered table.
* Native run `40139472497` and binding run `39941334444` both report SWIG
  **4.2.0**. A generator version mismatch is not established by this report.
* Qt's original `qtbase` v6.8.3
  `src/android/jar/src/org/qtproject/qt/android/CursorHandle.java` reads
  `m_cursorView.getDrawable()` in `width()` before checking initialization.

## Changes and checks

Pivy is imported after genuine GUI startup and before every camera gesture.
The first-shape tick checks the actual camera/scene bridge, logging
`COIN_BINDINGS`, `COIN_BRIDGE PASS` or `COIN_BRIDGE FAIL`, then performs
native axonometric framing and redraw even if the bridge fails. Packaging
also compares SWIG registry versions embedded in the actual ARM64 Base and
Pivy ELFs, rather than accepting their presence alone.

All scale presets launch the same custom QtActivity. Its content container
uses Android system-bar/cutout insets in physical pixels, before Qt paints
and routes touch. No independently scaled finger coordinates, second Qt
surface, GL reparenting, or fixed guessed status-bar heights are introduced.
Viewport mode B is the initial choice: existing native code obtains Coin
render/event dimensions from the actual GL widget and its DPR.

The original LGPL Qt source is retained in `qt-patches/CursorHandle.java`
with null guards for the view and drawable. The integration job compiles
the two original classes into the existing `Qt6Android.jar`, avoiding
duplicate DEX classes, then verifies the patched bytecode before deployment.

Host checks cover Pivy initialization order, first-shape framing despite a
bridge failure, coalesced touch scripts and Java compilation/bytecode. They
do not establish successful rendering or input on Android. Device acceptance:
clock/navigation bars do not cover commands; create a real primitive, check
visibility, orbit/pan/pinch, open a text field, select text, background/resume,
and collect the P07 log including `SAFE_AREA` and `COIN_BRIDGE` records.

The blank body is not proven to have a single cause. Default physical viewport
dimensions and verified camera initialization address identified faults;
the device still has to confirm actual Coin rendering.
