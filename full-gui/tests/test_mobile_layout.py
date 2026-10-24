#!/usr/bin/env python3
"""Cheap source-level gates for the DRNAS Android orientation contract.

Actual Qt geometry, Android safe area, touches and OpenGL still require
an installable APK test on the phone. This gate catches integration omissions.
"""
from pathlib import Path
import xml.etree.ElementTree as ET

base = Path(__file__).resolve().parents[1]
# Landscape-only experimental release: never create/rotate Qt's EGLSurface
# because of Android device rotation while Coin3D is active.
android_manifest = ET.parse(base / "qt-activity/android/AndroidManifest.xml")
android_ns = "{http://schemas.android.com/apk/res/android}"
android_activities = {
    activity.get(android_ns+"name"): activity.get(android_ns+"screenOrientation")
    for activity in android_manifest.findall(".//activity")
}
assert android_activities.get("org.drnas.freecad.bootstrap.StartupActivity") == "landscape", (
    "Splash activity must start in landscape before Qt creates its surface"
)
assert android_activities.get("org.qtproject.qt.android.bindings.QtActivity") == "landscape", (
    "Native FreeCAD QtActivity must not rotate with sensor orientation"
)

gui = (base / "qt-activity/IntegratedGui.cpp").read_text()
bridge = (base / "upstream/AndroidMainWindowBridge.cpp").read_text()
for source, checks in (
    (gui, (
        'class DrnasMobileWorkspace final : public QObject',
        'new DrnasMobileWorkspace(hostGuard.data(),cadGuard.data())',
        'height>width',
        'Qt::BottomDockWidgetArea',
        'Qt::LeftDockWidgetArea',
        'Qt::RightDockWidgetArea',
        'drnas_freecad_android_disable_overlays',
        'QApplication::activeModalWidget()',
        'MOBILE_LAYOUT',
        'drnasPortraitRestore',
        'STARTUP_UI P0 early native embed; controls deferred',
        'STARTUP_UI N1 native FreeCADGui returned; install command controls',
        'STARTUP_UI P5 touch controllers installed',
        'STARTUP_UI P6 stable; crash marker cleared',
        'nativeProbe/bootStamp',
        'drnasSuppressAutostart',
        'F1 copying Python stdlib and extension modules',
        'F1 OK; F2 unpacking original FreeCAD resources',
        'F2 OK; F3 initialize original FreeCAD GUI',
    )),
    (bridge, (
        '#include "OverlayManager.h"',
        'drnas_freecad_android_disable_overlays',
        'Gui::OverlayManager::OverlayMode::DisableAll',
    )),
):
    for token in checks:
        assert token in source, f"Android layout contract missing: {token}"
assert 'invoke(QStringLiteral("Std_DockOverlayToggleAll")' not in gui, "Overlay toggling is not safe on repeated rotations"
# Early integration must not install event filters before native startup
# finishes: the upstream stage callback calls processEvents() recursively.
early_embed = gui.split('bool embedOriginalFreeCADWindow(', 1)[1].split(
    'void recordOriginalGuiStage(', 1
)[0]
assert 'installOriginalFreeCADTouchControls(shell, window)' not in early_embed
assert 'installOriginalFreeCADTouchControls(this,freecadWindow)' in gui
assert 'settings.setValue(QStringLiteral("nativeProbe/pending"), result == 0)' in gui
assert 'if(property("drnasAutoStartPartDesign").toBool())' in gui
# Android layout must not run at 5-6 Hz on a static viewport: dock
# visibility/area change as a SIDE EFFECT of the layout pass.
assert 'previousLayout=layout;' in gui
assert 'needsDockRepair' not in gui
assert 'name.contains(QStringLiteral("selection"))' in gui
assert 'MOBILE_LAYOUT_STABLE' in gui
assert 'const bool essential=!bar->findChildren<QComboBox*>().isEmpty()' in gui


# A real Android 16 ARM64 log captured a native SIGSEGV at import MeasureGui
# before the MainWindow existed. Do not regress to eager import from the stage
# callback even when desktop FreeCAD expects Std_Measure commands.
assert 'MEASURE ISOLATED: skip unsafe early MeasureGui native import' in gui
assert '"import MeasureGui\\n"' not in gui, (
    "Native MeasureGui extension must not load during GUI startup"
)

# Input fixes share the same Android QWindow as the native FreeCAD scene.
# Enforce the regression contract before paying for the full Qt/ARM64 build.
touch = (base / "qt-activity/DrnasTouchInput.h").read_text()
for token in (
    'MODAL_EMBED prepared',
    'DlgActiveBody',
    'QEvent::Polish',
    'dialog->setParent(host,Qt::Widget)',
    'drnasModalRestoring',
    'layoutEmbeddedModal()',
    'embeddedDialog && embeddedDialog->isVisible()',
    'QPoint(0,zoomDeflection>0?120:-120)',
    'Qt::NoScrollPhase',
    'zoomTimer.setInterval(65)',
    'TOUCH_VIEW_BEGIN',
    'TOUCH_VIEW_RESIZE',
    'TOUCH_PAN_BEGIN',
    'TOUCH_PAN_END',
    'mouseButton(Qt::MiddleButton,false)',
    'suppressAfterPan',
    'lastViewportTouch.start()',
    'lastViewportTouch.elapsed()<650',
    'Qt::WA_AcceptTouchEvents',
):
    assert token in touch, f"Android modal/touch navigation contract missing: {token}"
assert 'queuedZoom' not in touch and 'previousZoom' not in touch, (
    "Half-notch wheel flood must not return"
)
print("PASS: Android adaptive layout, modal and touch navigation integration gates")

