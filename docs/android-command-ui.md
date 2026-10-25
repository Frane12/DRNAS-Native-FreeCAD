# Android command UI

The Android shell now presents the original FreeCAD commands in two rows above
the full-width CAD workspace. The first row pins New, Open, Save, Undo, Redo and
Fit when their original actions exist. The second row selects an original
toolbar/menu group and presents large icon-and-text buttons in a horizontal
finger-scrollable strip. Esc, RMB and Geste are in the header. Menu also exposes
the existing advanced tools and diagnostics.

## Ownership and behavior

`DrnasAndroidCommandUi.h` does not implement CAD commands. Each mirror uses
`QToolButton::setDefaultAction()` with the **same original QAction instance**.
FreeCAD continues to own the action and its activation handler. It also owns
the document, selection, task/property panels and Coin3D render widget.

- Command IDs come from the original `QAction::objectName()` when present.
  Upstream `Gui::Action` sets that to `Command::getName()` in `src/Gui/Action.cpp`.
  Some special toolbar actions, including Undo/Redo alternatives, are anonymous;
  their actual QAction objects remain usable without inventing an ID.
- Original icons, translations, tooltips, enabled/checkable/checked state and
  action groups stay live. An action is not reconstructed from copied strings.
- Each keyboard shortcut still belongs to its one original QAction. Attaching
  that action to Android buttons does not register a duplicate QAction shortcut.
- FreeCAD's `ActionGroup::addTo()` attaches some menus directly to QToolButton,
  without setting `QAction::menu()`. Mirrors retain the original source button's
  menu and popup mode as well as ordinary QAction menus. Original menu hooks
  remain connected, including dynamic menus.
- Source toolbars are retained with zero maximum size. They are not deleted or
  explicitly hidden by the Android shell. This preserves the visibility changes
  made by FreeCAD when switching workbenches. Visible source toolbar groups and
  original menu groups are collected after their actions are added/removed.
- WidgetActions that provide complex desktop widgets are not generically
  cloned. The original Workbench menu remains available through Menu and its
  command groups; original task/property forms remain in the CAD workspace.

The presentation uses Qt's existing palette and the original QIcons. It does
not generate replacement artwork or capture/resample FreeCAD's framebuffer.

## Input and layout

The custom header and tools are ordinary child widgets on the existing
QtActivity surface. The original FreeCAD MainWindow is embedded before its
first show; its render widget is not replaced or reparented by this UI.

The Android layout uses the full available width, replacing the 4:3 letterbox
workspace as the default. Startup display choices now describe standard, larger
and smaller Android controls. The scale is still set before Qt initializes;
all hit tests use the same logical coordinates as the widgets.

Command buttons have 48+ logical-pixel touch targets, and the tool strip uses
92 × 72 buttons with the original icons rendered at 28 logical pixels. A swipe
starting on either a button or an empty part of the strip moves its horizontal
scrollbar and cancels the pending command. A stationary tap produces one
balanced click through the existing 120 ms debounce. The UI does not rebuild
its button rows during an active finger sequence, embedded modal or popup.

The underlying desktop presentation can be selected by developers using
`DRNAS_ANDROID_COMMAND_UI=0` before startup. Normal Android launches explicitly
enable the new command UI.

## Verification

`tests/android-command-ui.cpp` sends finger input through the native QWindow.
It verifies original QAction ownership/identity, unchanged icon identity and
later icon updates, translated text, enabled/check state, the original shortcut,
New activation and debounce, ActionGroup popup ownership, finger scrolling
without command activation, group selection by finger, workbench show/hide
updates, full-width layout and retention of the original render widget.

The workflow runs this alongside the existing touch, modal and lifecycle tests,
including scale factors 1.5, 2 and 3. The Android packaging job compiles the
integration with Qt 6.8.3 and retains dependency/ABI/signature gates. These
checks do not replace testing the APK on a physical Android phone.

Phone check: start with standard controls, tap New, open Part/PartDesign from
the original menus, change the tool group, drag the strip and create geometry.
Confirm disabled tools and toggled modes match the original FreeCAD state.

Upstream reference: [FreeCAD Action.cpp](https://github.com/FreeCAD/FreeCAD/blob/d4ffc7f725973e073907746e88d320d3e438fd34/src/Gui/Action.cpp).
