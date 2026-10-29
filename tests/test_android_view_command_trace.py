import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('view_trace', ROOT / 'full-gui/upstream/patch-android-view-command-trace.py')
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)

FIXTURES = {"src/Gui/ApplicationPy.cpp":"PyObject* ApplicationPy::sGetMainWindow(PyObject* /*self*/, PyObject* args)\n{\n    if (!PyArg_ParseTuple(args, \"\")) {\n        return nullptr;\n    }\n\n    requirePythonMainThread(\"FreeCADGui.getMainWindow\");\n\n    try {\n        return Py::new_reference_to(MainWindowPy::createWrapper(Gui::getMainWindow()));\n    }\n    catch (const Py::Exception&) {\n        return nullptr;\n    }\n}\n\nPyObject* ApplicationPy::sUpdateGui(\n","src/Gui/MainWindowPy.cpp":"Py::Object MainWindowPy::createWrapper(MainWindow* mw)\n{\n    PythonWrapper wrap;\n    if (!wrap.loadCoreModule() || !wrap.loadGuiModule() || !wrap.loadWidgetsModule()) {\n        throw Py::RuntimeError(\"Failed to load Python wrapper for Qt\");\n    }\n\n    // copy attributes\n    static constexpr std::initializer_list<const char*> attr = {\n        \"getWindows\",\n        \"getWindowsOfType\",\n        \"setActiveWindow\",\n        \"getActiveWindow\",\n        \"addWindow\",\n        \"removeWindow\",\n        \"showHint\",\n        \"hideHint\",\n        \"addStatusBarItem\",\n        \"removeStatusBarItem\",\n    };\n\n    Py::Object py = wrap.fromQWidget(mw, \"QMainWindow\");\n    Py::ExtensionObject<MainWindowPy> inst(create(mw));\n    for (const auto& it : attr) {\n        py.setAttr(it, inst.getAttr(it));\n    }\n    return py;\n}\n\nMainWindowPy::MainWindowPy(\n\nPy::Object MainWindowPy::getActiveWindow(const Py::Tuple& args)\n{\n    if (!PyArg_ParseTuple(args.ptr(), \"\")) {\n        throw Py::Exception();\n    }\n\n    if (_mw) {\n        MDIView* mdi = _mw->activeWindow();\n        if (mdi) {\n            return Py::asObject(mdi->getPyObject());\n        }\n    }\n    return Py::None();\n}\n\nPy::Object MainWindowPy::addWindow(\n","src/Gui/MDIViewPy.cpp":"Py::Object MDIViewPy::sendMessage(const Py::Tuple& args)\n{\n    char* psMsgStr;\n    if (!PyArg_ParseTuple(args.ptr(), \"s;Message string needed (string)\", &psMsgStr)) {\n        throw Py::Exception();\n    }\n\n    try {\n        bool ok = false;\n        if (_view) {\n            ok = _view->onMsg(psMsgStr);\n        }\n        return Py::Boolean(ok);\n    }\n    catch (const Base::Exception& e) {\n        throw Py::RuntimeError(e.what());\n    }\n    catch (const std::exception& e) {\n        throw Py::RuntimeError(e.what());\n    }\n    catch (...) {\n        throw Py::RuntimeError(\"Unknown C++ exception\");\n    }\n}\n\nPy::Object MDIViewPy::supportMessage(\n","src/Gui/CommandView.cpp":"void StdCmdViewIsometric::activated(int iMsg)\n{\n    Q_UNUSED(iMsg);\n    doCommand(Command::Gui, \"Gui.getMainWindow().getActiveWindow().sendMessage(\\\"ViewAxo\\\")\");\n}\n\nbool StdCmdViewIsometric::isActive(\n","src/Gui/View3DInventor.cpp":"    else if (strcmp(\"ViewAxo\", pMsg) == 0) {\n        _viewer->setCameraOrientation(Camera::rotation(Camera::Isometric));\n        return true;\n    }\n    else if (strcmp(\"ViewDimetric\", pMsg) == 0) {\n"}

class ViewCommandTraceTest(unittest.TestCase):
    def make_root(self, temp):
        root = Path(temp)
        for name, contents in FIXTURES.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
        return root

    def test_all_checkpoints_and_original_command_are_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = self.make_root(temp)
            trace.patch(root)
            all_text = ''.join((root / name).read_text() for name in FIXTURES)
            for number in range(1, 11):
                self.assertIn('VIEW_CMD %02d' % number, all_text)
            self.assertIn('viewer->setCameraOrientation(Camera::rotation(Camera::Isometric));', all_text)
            self.assertIn('doCommand(Command::Gui,', all_text)

    def test_changed_anchor_fails_without_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = self.make_root(temp)
            path = root / 'src/Gui/MDIViewPy.cpp'
            path.write_text(path.read_text().replace('ok = _view->onMsg(psMsgStr);', 'changed();'))
            before = {name: (root / name).read_text() for name in FIXTURES}
            with self.assertRaises(RuntimeError):
                trace.patch(root)
            self.assertEqual(before, {name: (root / name).read_text() for name in FIXTURES})

if __name__ == '__main__':
    unittest.main()
