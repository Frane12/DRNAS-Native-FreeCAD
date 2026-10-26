import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('picker_patch', ROOT / 'full-gui/upstream/patch-android-sketch-plane-picker.py')
picker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(picker)

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
struct Object {};
struct Entry {
    Object* object;
    std::vector<std::string> subnames;
    Object* getObject() const { return object; }
    const std::vector<std::string>& getSubNames() const { return subnames; }
};
namespace Gui {
struct Selections { std::vector<Entry> entries; auto getSelectionEx() const { return entries; } };
Selections selected;
Selections& Selection() { return selected; }
}
struct Workflow {
    Object body, face;
    Object* activeBody = &body;
    int picked = 0, attachment = 0, bodies = 0;
    void createBodyOrThrow() { bodies++; }
    void findAndSelectPlane() { picked++; }
    void createSketchAndShowAttachment() { attachment++; }
'''
MAIN = r'''
};
int main() {
    Workflow w;
    auto check = [&](std::vector<Entry> entries, bool plane) {
        Gui::selected.entries = entries;
        w.picked = w.attachment = 0;
        w.tryFindSupport();
#ifdef __ANDROID__
        assert(w.picked == int(plane)); assert(w.attachment == int(!plane));
#else
        assert(w.picked == 0); assert(w.attachment == 1);
#endif
    };
    check({}, true);
    check({{&w.body, {}}}, true);
    check({{&w.body, {""}}}, true);
    check({{&w.body, {"Face1"}}}, false);
    check({{&w.face, {"Face1"}}}, false);
    check({{&w.face, {}}}, false);
    check({{&w.body, {}}, {&w.face, {"Face1"}}}, false);
    assert(w.bodies == 7);
}
'''

class SketchPlanePickerTest(unittest.TestCase):
    def test_selection_dispatch_compiles_on_android_and_desktop(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'test.cpp'
            path.write_text(HARNESS + picker.REPLACEMENT + MAIN)
            for flags, name in [(['-D__ANDROID__'], 'android'), ([], 'desktop')]:
                out = Path(tmp) / name
                subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra'] + flags + [str(path), '-o', str(out)], check=True, capture_output=True)
                subprocess.run([str(out)], check=True, capture_output=True)

    def test_source_drift_is_rejected_without_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / 'src/Mod/PartDesign/Gui/SketchWorkflow.cpp'
            path.parent.mkdir(parents=True)
            path.write_text('using namespace PartDesignGui;\n#include <map>\nchanged upstream\n')
            before = path.read_text()
            with self.assertRaises(RuntimeError): picker.patch(root)
            self.assertEqual(before, path.read_text())

if __name__ == '__main__': unittest.main()
