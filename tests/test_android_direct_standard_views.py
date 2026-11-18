import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('direct_views', ROOT / 'full-gui/upstream/patch-android-direct-standard-views.py')
direct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(direct)

FIXTURE = "void StdCmdViewBottom::activated(int iMsg)\n{\n    Q_UNUSED(iMsg);\n    doCommand(Command::Gui, \"Gui.getMainWindow().getActiveWindow().sendMessage(\\\"ViewBottom\\\")\");\n}\n\nvoid StdCmdViewFront::activated(int iMsg)\n{\n    Q_UNUSED(iMsg);\n    doCommand(Command::Gui, \"Gui.getMainWindow().getActiveWindow().sendMessage(\\\"ViewFront\\\")\");\n}\n\nvoid StdCmdViewLeft::activated(int iMsg)\n{\n    Q_UNUSED(iMsg);\n    doCommand(Command::Gui, \"Gui.getMainWindow().getActiveWindow().sendMessage(\\\"ViewLeft\\\")\");\n}\n\nvoid StdCmdViewRear::activated(int iMsg)\n{\n    Q_UNUSED(iMsg);\n    doCommand(Command::Gui, \"Gui.getMainWindow().getActiveWindow().sendMessage(\\\"ViewRear\\\")\");\n}\n\nvoid StdCmdViewRight::activated(int iMsg)\n{\n    Q_UNUSED(iMsg);\n    doCommand(Command::Gui, \"Gui.getMainWindow().getActiveWindow().sendMessage(\\\"ViewRight\\\")\");\n}\n\nvoid StdCmdViewTop::activated(int iMsg)\n{\n    Q_UNUSED(iMsg);\n    doCommand(Command::Gui, \"Gui.getMainWindow().getActiveWindow().sendMessage(\\\"ViewTop\\\")\");\n}\n\nvoid StdCmdViewIsometric::activated(int iMsg)\n{\n    Q_UNUSED(iMsg);\n    doCommand(Command::Gui, \"Gui.getMainWindow().getActiveWindow().sendMessage(\\\"ViewAxo\\\")\");\n}"
HARNESS = r'''
#include <cassert>
#include <string>
#include <vector>
#define Q_UNUSED(x) (void)(x)
namespace Command { enum { Gui }; }
std::string pythonCall, lastPhase;
void drnasDocumentPhase(const char*, const char* phase) { lastPhase = phase; }
struct App {
    bool active = true;
    std::vector<std::string> calls;
    bool sendMsgToActiveView(const char* message) {
        if (active) calls.push_back(message);
        return active;
    }
};
App app;
App* getGuiApplication() { return &app; }
void doCommand(int, const char* text) { pythonCall = text; }
'''
MAIN = r'''
int main() {
    using Method = void (*)(int);
    const Method methods[] = {
        &StdCmdViewBottom::activated, &StdCmdViewFront::activated,
        &StdCmdViewLeft::activated, &StdCmdViewRear::activated,
        &StdCmdViewRight::activated, &StdCmdViewTop::activated,
        &StdCmdViewIsometric::activated
    };
    const char* expected[] = {
        "ViewBottom", "ViewFront", "ViewLeft", "ViewRear",
        "ViewRight", "ViewTop", "ViewAxo"
    };
    for (int i=0; i<7; ++i) {
        pythonCall.clear();
        methods[i](0);
#ifdef __ANDROID__
        assert(app.calls.back() == expected[i]);
        assert(pythonCall.empty());
        assert(lastPhase == "END");
#else
        assert(pythonCall.find(expected[i]) != std::string::npos);
        assert(app.calls.empty());
#endif
    }
#ifdef __ANDROID__
    auto count = app.calls.size();
    app.active = false;
    methods[6](0);
    assert(app.calls.size() == count);
    assert(lastPhase == "NO_VIEW");
#endif
}
'''

class DirectStandardViewsTest(unittest.TestCase):
    def test_android_and_desktop_routes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / 'src/Gui/CommandView.cpp'
            path.parent.mkdir(parents=True)
            path.write_text(FIXTURE)
            direct.patch(root)
            patched = path.read_text()
            start = patched.index('void StdCmdViewBottom::activated(')
            methods = patched[start:]
            names = list(direct.VIEWS)
            declarations = '\n'.join('struct ' + n + ' { static void activated(int); };' for n in names)
            harness = HARNESS + declarations + '\n' + methods + '\n' + MAIN
            cpp = root / 'test.cpp'
            cpp.write_text(harness)
            for android in (False, True):
                binary = root / ('android' if android else 'desktop')
                args = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror']
                if android:
                    args.append('-D__ANDROID__')
                subprocess.run(args + [str(cpp), '-o', str(binary)], check=True, capture_output=True, text=True)
                subprocess.run([str(binary)], check=True)

    def test_source_drift_does_not_write(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / 'src/Gui/CommandView.cpp'
            path.parent.mkdir(parents=True)
            source = FIXTURE.replace('ViewAxo', 'Unexpected')
            path.write_text(source)
            with self.assertRaises(RuntimeError):
                direct.patch(root)
            self.assertEqual(path.read_text(), source)

if __name__ == '__main__':
    unittest.main()
