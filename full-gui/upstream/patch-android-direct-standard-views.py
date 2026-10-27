#!/usr/bin/env python3
"""Route original FreeCAD standard view commands directly on Android."""
from pathlib import Path
import sys

VIEWS = {
    "StdCmdViewBottom": "ViewBottom",
    "StdCmdViewFront": "ViewFront",
    "StdCmdViewLeft": "ViewLeft",
    "StdCmdViewRear": "ViewRear",
    "StdCmdViewRight": "ViewRight",
    "StdCmdViewTop": "ViewTop",
    "StdCmdViewIsometric": "ViewAxo",
}
MARKER = '''#ifdef __ANDROID__
#include <Base/AndroidDocumentDiagnostics.h>
extern "C" __attribute__((visibility("default")))
int drnas_android_direct_standard_views_v1() { return 1; }
#endif
'''

def patch(root):
    path = root / 'src/Gui/CommandView.cpp'
    source = path.read_text()
    changed = source
    for command, message in VIEWS.items():
        signature = 'void ' + command + '::activated(int iMsg)'
        start = changed.index(signature)
        end = changed.index('\n}', start) + 2
        body = changed[start:end]
        command_line = '    doCommand(Command::Gui, "Gui.getMainWindow().getActiveWindow().sendMessage(\\\"' + message + '\\\")");'
        if body.count(command_line) != 1:
            raise RuntimeError('Standard view command changed: ' + command)
        replacement = (
            '#ifdef __ANDROID__\n'
            '    drnasDocumentPhase("VIEW_DIRECT ' + message + '", "BEGIN");\n'
            '    const bool drnasHandled = getGuiApplication()->sendMsgToActiveView("' + message + '");\n'
            '    drnasDocumentPhase("VIEW_DIRECT ' + message + '", drnasHandled ? "END" : "NO_VIEW");\n'
            '#else\n'
            + command_line + '\n'
            '#endif'
        )
        body = body.replace(command_line, replacement, 1)
        changed = changed[:start] + body + changed[end:]
    if 'drnas_android_direct_standard_views_v1' in changed:
        raise RuntimeError('Standard view patch already applied')
    path.write_text(MARKER + changed)

if __name__ == '__main__':
    patch(Path(sys.argv[1]))
    print('ANDROID_DIRECT_STANDARD_VIEWS_V1: genuine FreeCAD messages on active native view')
