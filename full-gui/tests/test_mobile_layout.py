#!/usr/bin/env python3
"""Cheap source-level gates for the DRNAS Android orientation contract.

Actual Qt geometry, Android safe area, touches and OpenGL still require
an installable APK test on the phone. This gate catches integration omissions.
"""
from pathlib import Path

base = Path(__file__).resolve().parents[1]
gui = (base / "qt-activity/IntegratedGui.cpp").read_text()
bridge = (base / "upstream/AndroidMainWindowBridge.cpp").read_text()
for source, checks in (
    (gui, (
        'class DrnasMobileWorkspace final : public QObject',
        'new DrnasMobileWorkspace(shell,freecad)',
        'height>width',
        'Qt::BottomDockWidgetArea',
        'Qt::LeftDockWidgetArea',
        'Qt::RightDockWidgetArea',
        'drnas_freecad_android_disable_overlays',
        'QApplication::activeModalWidget()',
        'MOBILE_LAYOUT',
        'drnasPortraitRestore',
    )),
    (bridge, (
        '#include "OverlayManager.h"',
        'drnas_freecad_android_disable_overlays',
        'Gui::OverlayManager::OverlayMode::DisableAll',
    )),
):
    for token in checks:
        assert token in source, f"Android layout contract missing: {token}"
assert 'Std_DockOverlayToggleAll' not in gui, "Overlay toggling is not safe on repeated rotations"
print("PASS: Android adaptive layout static integration gates")
