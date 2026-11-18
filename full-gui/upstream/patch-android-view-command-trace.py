#!/usr/bin/env python3
"""Add Android-only persisted checkpoints to the standard isometric view path."""
from pathlib import Path
import sys

HEADER = '''#ifdef __ANDROID__
#include <Base/AndroidDocumentDiagnostics.h>
extern thread_local bool drnasViewTraceActive;
#define DRNAS_VIEW_MARK(stage, phase) do { if (drnasViewTraceActive) drnasDocumentPhase(stage, phase); } while (false)
#endif
'''
OWNER = '''#ifdef __ANDROID__
#include <Base/AndroidDocumentDiagnostics.h>
thread_local bool drnasViewTraceActive = false;
struct DrnasViewTraceScope {
    DrnasViewTraceScope() { drnasViewTraceActive = true; drnasDocumentPhase("VIEW_CMD 01 activate", "BEGIN"); }
    ~DrnasViewTraceScope() { drnasDocumentPhase("VIEW_CMD 01 activate", "END"); drnasViewTraceActive = false; }
};
#endif
'''

def once(src, old, new):
    n = src.count(old)
    if n != 1:
        raise RuntimeError(f"View trace source anchor changed: {old[:120]!r}, count={n}")
    return src.replace(old, new, 1)

def mark_statement(section, statement, name, end=True):
    start = '#ifdef __ANDROID__\n    DRNAS_VIEW_MARK("' + name + '", "BEGIN");\n#endif\n'
    finish = '\n#ifdef __ANDROID__\n    DRNAS_VIEW_MARK("' + name + '", "END");\n#endif' if end else ''
    return once(section, statement, start + statement + finish)

def edit_section(src, start, end, edits):
    a = src.index(start)
    b = src.index(end, a)
    section = src[a:b]
    for statement, name, complete in edits:
        section = mark_statement(section, statement, name, complete)
    return src[:a] + section + src[b:]

def patch(root):
    paths = {
        'command': root / 'src/Gui/CommandView.cpp',
        'app': root / 'src/Gui/ApplicationPy.cpp',
        'window': root / 'src/Gui/MainWindowPy.cpp',
        'mdi': root / 'src/Gui/MDIViewPy.cpp',
        'view': root / 'src/Gui/View3DInventor.cpp',
    }
    original = {key: path.read_text() for key, path in paths.items()}
    changed = {}
    s = original['command']
    anchor = 'void StdCmdViewIsometric::activated(int iMsg)\n{'
    s = once(s, anchor, anchor + '\n#ifdef __ANDROID__\n    DrnasViewTraceScope drnasViewScope;\n#endif')
    changed['command'] = OWNER + s

    s = edit_section(original['app'],
        'PyObject* ApplicationPy::sGetMainWindow(',
        '\nPyObject* ApplicationPy::sUpdateGui(', [
            ('        return Py::new_reference_to(MainWindowPy::createWrapper(Gui::getMainWindow()));',
             'VIEW_CMD 02 get main window wrapper', False),
        ])
    changed['app'] = HEADER + s

    s = edit_section(original['window'],
        'Py::Object MainWindowPy::createWrapper(',
        '\nMainWindowPy::MainWindowPy(', [
            ('    if (!wrap.loadCoreModule() || !wrap.loadGuiModule() || !wrap.loadWidgetsModule()) {\n        throw Py::RuntimeError("Failed to load Python wrapper for Qt");\n    }', 'VIEW_CMD 03 load Qt bindings', True),
            ('    Py::Object py = wrap.fromQWidget(mw, "QMainWindow");', 'VIEW_CMD 04 wrap main window', True),
            ('    Py::ExtensionObject<MainWindowPy> inst(create(mw));', 'VIEW_CMD 05 create wrapper', True),
            ('        py.setAttr(it, inst.getAttr(it));', 'VIEW_CMD 06 bind attribute', True),
        ])
    s = edit_section(s,
        'Py::Object MainWindowPy::getActiveWindow(',
        '\nPy::Object MainWindowPy::addWindow(', [
            ('        MDIView* mdi = _mw->activeWindow();', 'VIEW_CMD 07 active window', True),
            ('            return Py::asObject(mdi->getPyObject());', 'VIEW_CMD 08 wrap active view', False),
        ])
    changed['window'] = HEADER + s

    s = edit_section(original['mdi'],
        'Py::Object MDIViewPy::sendMessage(',
        '\nPy::Object MDIViewPy::supportMessage(', [
            ('            ok = _view->onMsg(psMsgStr);', 'VIEW_CMD 09 send view message', True),
        ])
    changed['mdi'] = HEADER + s

    s = edit_section(original['view'],
        '    else if (strcmp("ViewAxo", pMsg) == 0) {',
        '    else if (strcmp("ViewDimetric", pMsg) == 0) {', [
            ('        _viewer->setCameraOrientation(Camera::rotation(Camera::Isometric));',
             'VIEW_CMD 10 set isometric camera', True),
        ])
    changed['view'] = HEADER + s

    # Validate every anchor before writing any file.
    for key, path in paths.items():
        path.write_text(changed[key])

if __name__ == '__main__':
    patch(Path(sys.argv[1]))
    print('VIEW_CMD_TRACE_V1: isometric command and Python wrapper checkpoints')
