"""Smoke-check exact native diagnostic injection against pinned source fragments."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "full-gui/upstream/patch-android-type-shiboken-diagnostics.py"
spec = importlib.util.spec_from_file_location("drnas_native_probe", PATCH)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

FRAGMENTS = {
    "src/Mod/PartDesign/App/AppPartDesign.cpp":
        "    PartDesign::Body                        ::init();\n",
    "src/Mod/Sketcher/App/AppSketcher.cpp":
        "    Sketcher::SketchObject::init();\n",
    "src/App/GeoFeatureGroupExtension.cpp":
        "    if (!obj->isDerivedFrom<App::GeoFeature>()) {\n"
        '        throw Base::RuntimeError("GeoFeatureGroupExtension can only be applied to GeoFeatures");\n'
        "    }\n",
    "src/Gui/PythonWrapper.cpp":
        "Py::Object PythonWrapper::fromQWidget(QWidget* widget, const char* className)\n"
        "{\n    return Py::None();\n}\n",
}


class NativeReadOnlyProbeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for relative, contents in FRAGMENTS.items():
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(contents, encoding="utf-8")

    def test_patches_only_instrumentation(self):
        module.patch_source(self.root)
        expected = {
            "src/Mod/PartDesign/App/AppPartDesign.cpp": "[DRNAS_TYPE]",
            "src/Mod/Sketcher/App/AppSketcher.cpp": "[DRNAS_TYPE]",
            "src/App/GeoFeatureGroupExtension.cpp": "[DRNAS_GEO_REJECT]",
            "src/Gui/PythonWrapper.cpp": "[DRNAS_SHIBOKEN_PTR]",
        }
        for relative, marker in expected.items():
            with self.subTest(file=relative):
                source = (self.root / relative).read_text()
                self.assertIn(marker, source)
                self.assertIn("#ifdef __ANDROID__", source)

    def test_refuses_source_drift(self):
        path = self.root / "src/Mod/PartDesign/App/AppPartDesign.cpp"
        path.write_text("not FreeCAD source\n", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "Pinned FreeCAD source changed"):
            module.patch_source(self.root)

    def test_refuses_repeat_injection(self):
        module.patch_source(self.root)
        with self.assertRaisesRegex(RuntimeError, "Already instrumented"):
            module.patch_source(self.root)


if __name__ == "__main__":
    unittest.main()
