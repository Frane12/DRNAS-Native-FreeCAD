#!/usr/bin/env python3
"""Preserve the exception and activation phase in the Android startup error."""
from pathlib import Path
import sys


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Pinned FreeCAD source changed: {old[:100]!r}")
    return source.replace(old, new, 1)


def patch(root):
    # PyCXX exceptions do not derive from std::exception. Inspect the active
    # exception before replacing it, including its Python error indicator.
    header = root / "src/Gui/AndroidWorkbenchDiagnostics.h"
    header.write_text(r'''#pragma once
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <CXX/Objects.hxx>
#include <cxxabi.h>
#include <cstdlib>
#include <exception>
#include <string>
#include <typeinfo>

namespace {
std::string drnasCurrentException()
{
    try { throw; }
    catch (Py::Exception&) {
        Base::PyException error;
        return std::string("Python: ") + error.what() + "\n" + error.getStackTrace();
    }
    catch (const Base::Exception& error) { return error.what(); }
    catch (const std::exception& error) {
        return std::string(typeid(error).name()) + ": " + error.what();
    }
    catch (const char* error) { return error ? error : "null C-string exception"; }
    catch (const std::string& error) { return error; }
    catch (...) {
        // Android shared-library RTTI can leave a PyCXX exception unmatched.
        // Read CPython's error indicator without another PyCXX conversion.
        if (PyErr_Occurred()) {
            PyObject* error = PyErr_GetRaisedException();
            PyObject* message = error ? PyObject_Str(error) : nullptr;
            const char* utf8 = message ? PyUnicode_AsUTF8(message) : nullptr;
            std::string detail = utf8 ? utf8 : "unprintable Python exception";
            Py_XDECREF(message);
            Py_XDECREF(error);
            PyErr_Clear();
            return "Python: " + detail;
        }
        const std::type_info* type = abi::__cxa_current_exception_type();
        if (!type) { return "unknown C++ exception type"; }
        int status = 0;
        char* decoded = abi::__cxa_demangle(type->name(), nullptr, nullptr, &status);
        std::string name = status == 0 && decoded ? decoded : type->name();
        std::free(decoded);
        return "C++ exception type: " + name;
    }
}
}
''')
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
            + ": " + drnasCurrentException());
    }
}
'''
    source = replace_once(source, old, new)
    path.write_text('#include "AndroidWorkbenchDiagnostics.h"\n' + source)

    # Workbench.Icon is optional and the upstream base class sets it to None.
    # Do not construct Py::String(None), then rely on a cross-DSO exception
    # catch for ordinary absence of an icon. Retain the normal window-icon
    # fallback below this block and preserve all existing string/icon paths.
    path = root / "src/Gui/Application.cpp"
    source = path.read_text()
    start = source.index("QPixmap Application::workbenchIcon(")
    end = source.index("QString Application::workbenchToolTip(", start)
    old = source[start:end]
    new = replace_once(old,
        '                Py::String data(member);',
        '                if (member.isString()) {\n                Py::String data(member);')
    new = replace_once(new,
        '                return icon;\n            }',
        '                return icon;\n                }\n            }')
    source = replace_once(source, old, new)
    path.write_text(source)

    # Annotate both overloads, including recursive submenu construction.
    # Keep all real commands; a failed menu must still fail initialization.
    path = root / "src/Gui/MenuManager.cpp"
    source = path.read_text()
    for signature, following in [
        ("void MenuManager::setup(MenuItem* menuItems) const", "void MenuManager::setup(MenuItem* item, QMenu* menu) const"),
        ("void MenuManager::setup(MenuItem* item, QMenu* menu) const", "void MenuManager::retranslate() const"),
    ]:
        start = source.index(signature + "\n{")
        end = source.index(following, start + len(signature))
        old = source[start:end]
        body = old.removeprefix(signature + "\n{").rstrip()
        assert body.endswith("}")
        body = body[:-1]
        for loop in ["for (auto& item : menuItems->getItems()) {", "for (auto& item : item->getItems()) {"]:
            if loop in body:
                body = replace_once(body, loop, loop + '\n        currentItem = item->command();')
        if "mgr.addTo(item->command().c_str(), menu)" in body:
            body = replace_once(body, "if (mgr.addTo(item->command().c_str(), menu)) {",
                'phase = "CommandManager::addTo";\n                    if (mgr.addTo(item->command().c_str(), menu)) {')
        new = signature + '''
{
    std::string currentItem = "<menu bar>";
    const char* phase = "create menu";
    try {''' + body + '''    }
    catch (...) {
        throw Base::RuntimeError(std::string("Menu item ") + currentItem
            + " / " + phase + ": " + drnasCurrentException());
    }
}

'''
        source = replace_once(source, old, new)
    path.write_text('#include "AndroidWorkbenchDiagnostics.h"\n' + source)


if __name__ == "__main__":
    patch(Path(sys.argv[1]))
    print("Android workbench errors retain C++ exception details and activation phase")
