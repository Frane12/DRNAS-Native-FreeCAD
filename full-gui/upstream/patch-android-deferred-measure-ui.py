#!/usr/bin/env python3
"""Only insert deferred Android Measure actions after genuine registration."""
from pathlib import Path
import sys

MENU = '''    *tool << "Std_Measure"
          << "Std_MassProperties"
          << "Std_UnitsCalculator"'''
MENU_READY = '''    // DRNAS_MEASURE_MENU_BEGIN
#ifdef __ANDROID__
    if (Application::Instance->commandManager().getCommandByName("Std_Measure")) {
        *tool << "Std_Measure";
    }
    if (Application::Instance->commandManager().getCommandByName("Std_MassProperties")) {
        *tool << "Std_MassProperties";
    }
#else
    *tool << "Std_Measure" << "Std_MassProperties";
#endif
    // DRNAS_MEASURE_MENU_END
    *tool << "Std_UnitsCalculator"'''
TOOLBAR = '''          << "Separator" << "Std_DrawStyle" << "Separator"
          << "Std_Measure" << "Std_MassProperties";'''
TOOLBAR_READY = '''          << "Separator" << "Std_DrawStyle";
    // DRNAS_MEASURE_TOOLBAR_BEGIN
#ifdef __ANDROID__
    const auto& measureCommands = Application::Instance->commandManager();
    if (measureCommands.getCommandByName("Std_Measure")
        || measureCommands.getCommandByName("Std_MassProperties")) {
        *view << "Separator";
    }
    if (measureCommands.getCommandByName("Std_Measure")) {
        *view << "Std_Measure";
    }
    if (measureCommands.getCommandByName("Std_MassProperties")) {
        *view << "Std_MassProperties";
    }
#else
    *view << "Separator" << "Std_Measure" << "Std_MassProperties";
#endif
    // DRNAS_MEASURE_TOOLBAR_END'''
MARKER = '''
#ifdef __ANDROID__
extern "C" __attribute__((visibility("default")))
int drnas_android_deferred_measure_ui_v1() { return 1; }
#endif
'''


def patch(root):
    path = root / "src/Gui/Workbench.cpp"
    source = path.read_text()
    replacements = [(MENU, MENU_READY), (TOOLBAR, TOOLBAR_READY),
                    ("using namespace Gui;\n", "using namespace Gui;\n" + MARKER)]
    for old, _ in replacements:
        if source.count(old) != 1:
            raise RuntimeError(f"Pinned Workbench Measure UI anchor changed: {old[:90]!r}")
    for old, new in replacements:
        source = source.replace(old, new, 1)
    path.write_text(source)


if __name__ == "__main__":
    patch(Path(sys.argv[1]))
    print("MEASURE_UI_V1: Android menus and toolbar wait for genuine command registration")
