#!/usr/bin/env python3
"""Preserve the exception and activation phase in the Android startup error."""
from pathlib import Path
import sys


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Pinned FreeCAD source changed: {old[:100]!r}")
    return source.replace(old, new, 1)


def patch(root):
    path = root / "src/Gui/ApplicationPy.cpp"
    source = path.read_text()
    old = '''    catch (...) {
        std::stringstream err;
        err << "Unknown C++ exception raised in activateWorkbench('" << psKey << "')";'''
    new = '''    catch (const std::exception& e) {
        std::stringstream err;
        err << "C++ exception in activateWorkbench('" << psKey << "'): "
            << typeid(e).name() << ": " << e.what();
        PyErr_SetString(PyExc_RuntimeError, err.str().c_str());
        return nullptr;
    }
    catch (...) {
        std::stringstream err;
        err << "Unknown C++ exception raised in activateWorkbench('" << psKey << "')";'''
    source = replace_once(source, old, new)
    path.write_text('#include <exception>\n#include <typeinfo>\n' + source)

    path = root / "src/Gui/Workbench.cpp"
    source = path.read_text()
    start = source.index("bool Workbench::activate()\n{")
    end = source.index("\nvoid Workbench::retranslate()", start)
    old = source[start:end]
    body = old.removeprefix("bool Workbench::activate()\n{").rstrip()
    assert body.endswith("}")
    body = body[:-1]
    # Keep the actual activation calls and their order. Only annotate the
    # phase so an exception cannot be reduced to an unhelpful catch-all.
    phases = [
        ("ToolBarItem* tb = setupToolBars();", "setupToolBars"),
        ('setupCustomToolbars(tb, "Toolbar");', "setupCustomToolbars"),
        ("WorkbenchManipulator::changeToolBars(tb);", "changeToolBars"),
        ("ToolBarManager::getInstance()->setup(tb);", "ToolBarManager::setup"),
        ("DockWindowItems* dw = setupDockWindows();", "setupDockWindows"),
        ("WorkbenchManipulator::changeDockWindows(dw);", "changeDockWindows"),
        ("DockWindowManager::instance()->setup(dw);", "DockWindowManager::setup"),
        ("MenuItem* mb = setupMenuBar();", "setupMenuBar"),
        ("addPermanentMenuItems(mb);", "addPermanentMenuItems"),
        ("WorkbenchManipulator::changeMenuBar(mb);", "changeMenuBar"),
        ("MenuManager::getInstance()->setup(mb);", "MenuManager::setup"),
        ("setupCustomShortcuts();", "setupCustomShortcuts"),
    ]
    for statement, phase in phases:
        body = replace_once(body, statement, f'phase = "{phase}";\n    {statement}')
    new = '''bool Workbench::activate()
{
    const char* phase = "begin";
    try {''' + body + '''    }
    catch (const Base::Exception& e) {
        throw Base::RuntimeError(std::string("Workbench activation / ") + phase + ": " + e.what());
    }
    catch (const std::exception& e) {
        throw Base::RuntimeError(std::string("Workbench activation / ") + phase
            + ": " + typeid(e).name() + ": " + e.what());
    }
    catch (...) {
        throw Base::RuntimeError(std::string("Workbench activation / ") + phase
            + ": non-standard C++ exception");
    }
}
'''
    source = replace_once(source, old, new)
    path.write_text('#include <Base/Exception.h>\n#include <exception>\n#include <string>\n#include <typeinfo>\n' + source)


if __name__ == "__main__":
    patch(Path(sys.argv[1]))
    print("Android workbench errors retain C++ exception details and activation phase")
