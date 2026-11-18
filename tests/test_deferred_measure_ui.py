"""Execute generated native menu composition and the post-import refresh."""
import ast
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'measure_ui_patch', ROOT / 'full-gui/upstream/patch-android-deferred-measure-ui.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)

PINNED_FRAGMENT = '''using namespace Gui;
void composeMenu(Item* tool) {
    *tool << "Std_Measure"
          << "Std_MassProperties"
          << "Std_UnitsCalculator"
          << "OtherCommand";
}
void composeToolbar(Item* view) {
    *view << "Std_ViewFitAll"
          << "Separator" << "Std_DrawStyle" << "Separator"
          << "Std_Measure" << "Std_MassProperties";
}
'''

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <set>
#include <string>
#include <vector>
namespace Gui {
struct Item {
    std::vector<std::string> commands;
    Item& operator<<(const char* name) { commands.emplace_back(name); return *this; }
};
struct Manager {
    std::set<std::string> registered;
    bool getCommandByName(const char* name) const { return registered.count(name); }
};
struct Application {
    static Application* Instance;
    Manager manager;
    Manager& commandManager() { return manager; }
};
Application app;
Application* Application::Instance = &app;
}
'''
ASSERTIONS = r'''
int main() {
    for (unsigned mask = 0; mask < 4; ++mask) {
        Gui::app.manager.registered.clear();
        const char* names[] = {"Std_Measure", "Std_MassProperties"};
        for (unsigned i = 0; i < 2; ++i)
            if (mask & (1u << i)) Gui::app.manager.registered.insert(names[i]);
        for (int rebuild = 0; rebuild < 20; ++rebuild) {
            Gui::Item menu, bar;
            composeMenu(&menu); composeToolbar(&bar);
            for (unsigned i = 0; i < 2; ++i) {
#ifdef __ANDROID__
                bool expected = mask & (1u << i);
#else
                bool expected = true;
#endif
                for (const auto* item : {&menu, &bar})
                    assert((std::count(item->commands.begin(), item->commands.end(), names[i]) == 1) == expected);
            }
            assert(menu.commands.back() == "OtherCommand");
            assert(std::count(menu.commands.begin(), menu.commands.end(), "Std_UnitsCalculator") == 1);
            assert(bar.commands.front() == "Std_ViewFitAll");
            assert(std::count(bar.commands.begin(), bar.commands.end(), "Std_DrawStyle") == 1);
            // An unavailable optional command must not leave a trailing separator.
#ifdef __ANDROID__
            if (mask == 0) assert(bar.commands.back() == "Std_DrawStyle");
#endif
        }
    }
}
'''


class NativeMeasureMenuTest(unittest.TestCase):
    def test_compiled_android_and_desktop_composition(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / 'src/Gui/Workbench.cpp'
            path.parent.mkdir(parents=True)
            path.write_text(PINNED_FRAGMENT)
            PATCH.patch(root)
            source = path.read_text()
            path.write_text(HARNESS + source + ASSERTIONS)
            for platform in ('android', 'desktop'):
                binary = root / platform
                args = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror']
                if platform == 'android':
                    args += ['-D__ANDROID__']
                subprocess.run(args + [str(path), '-o', str(binary)], check=True,
                               capture_output=True, text=True)
                subprocess.run([str(binary)], check=True)

    def test_drift_fails_before_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / 'src/Gui/Workbench.cpp'
            path.parent.mkdir(parents=True)
            source = PINNED_FRAGMENT.replace('Std_UnitsCalculator', 'ChangedCommand')
            path.write_text(source)
            with self.assertRaises(RuntimeError):
                PATCH.patch(root)
            self.assertEqual(path.read_text(), source)


class MeasureRefreshTest(unittest.TestCase):
    def setUp(self):
        source = (ROOT / 'full-gui/integrated-apk/drnas_android_runtime.py').read_text()
        tree = ast.parse(source)
        node = next(n for n in tree.body if isinstance(n, ast.FunctionDef)
                    and n.name == '_drnas_refresh_measure_commands')
        self.messages = []
        self.namespace = {'_drnas_mark': self.messages.append}
        exec(compile(ast.Module(body=[node], type_ignores=[]), '<measure refresh>', 'exec'),
             self.namespace)
        self.refresh = self.namespace['_drnas_refresh_measure_commands']
        self.calls = 0
        self.commands = ['Std_Measure', 'Std_MassProperties']
        self.workbench = types.SimpleNamespace(reloadActive=self.reload)
        gui = types.SimpleNamespace(listCommands=lambda: self.commands,
                                    activeWorkbench=lambda: self.workbench)
        self.mock_modules = patch.dict(sys.modules, {'FreeCADGui': gui})
        self.mock_modules.start()
        self.addCleanup(self.mock_modules.stop)

    def reload(self):
        self.calls += 1

    def test_registered_commands_refresh_only_once(self):
        self.assertTrue(self.refresh())
        self.assertTrue(self.refresh())
        self.assertEqual(self.calls, 1)

    def test_missing_native_command_is_reported_without_refresh(self):
        self.commands.remove('Std_MassProperties')
        with self.assertRaisesRegex(RuntimeError, 'Std_MassProperties'):
            self.refresh()
        self.assertEqual(self.calls, 0)
        self.assertNotIn('_drnas_measure_ui_ready', self.namespace)

    def test_no_active_workbench_waits_for_next_activation(self):
        self.workbench = None
        self.assertFalse(self.refresh())
        self.workbench = types.SimpleNamespace(reloadActive=self.reload)
        self.assertTrue(self.refresh())
        self.assertEqual(self.calls, 1)

    def test_refresh_failure_remains_visible_and_can_be_retried(self):
        def fail():
            raise RuntimeError('real menu setup failure')
        self.workbench.reloadActive = fail
        with self.assertRaisesRegex(RuntimeError, 'real menu setup failure'):
            self.refresh()
        self.assertNotIn('_drnas_measure_ui_ready', self.namespace)
        self.workbench.reloadActive = self.reload
        self.assertTrue(self.refresh())


if __name__ == '__main__':
    unittest.main()
