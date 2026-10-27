#!/usr/bin/env python3
"""Keep FreeCAD action-group tooltips from creating Android popup windows."""
from pathlib import Path
import sys

def patch(root):
    path = root / "src/Gui/Action.cpp"
    source = path.read_text()
    anchor = """void ActionGroup::onHovered(QAction* act)
{
    if (!act) {
        return;
    }
"""
    replacement = anchor + """
#ifdef __ANDROID__
    // FreeCAD opens this tooltip directly, bypassing Qt's ToolTip event.
    // On Android its extra top-level window disrupts the active menu.
    // Retain normal tooltips elsewhere and all action activation behavior.
    if (qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
        QToolTip::hideText();
        return;
    }
#endif
"""
    if source.count(anchor) != 1 or "DRNAS_ANDROID_MENU_TOOLTIP" in source:
        raise RuntimeError("FreeCAD action-group tooltip anchor changed or patch already applied")
    path.write_text(source.replace(anchor, replacement + "    // DRNAS_ANDROID_MENU_TOOLTIP\n", 1))

if __name__ == "__main__":
    patch(Path(sys.argv[1]))
    print("ANDROID_MENU_TOOLTIP: suppress direct action-group tooltip for active Android menus")
