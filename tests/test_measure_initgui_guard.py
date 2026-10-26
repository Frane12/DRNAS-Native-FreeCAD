"""Regression tests for the Android-only FreeCAD Measure/InitGui.py import gate."""
import builtins
import os
import runpy
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "full-gui/integrated-apk/stage-freecad-assets.py"
PATCH = runpy.run_path(str(SCRIPT))["defer_measure_init_gui"]

ORIGINAL = (
    "# upstream FreeCAD startup note\n"
    "# imported for default measurement tools\n"
    "import Measure\n"
    "import MeasureGui\n"
)


class MeasureInitGuiGateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.target = Path(self.temp.name) / "InitGui.py"
        self.target.write_text(ORIGINAL, encoding="utf-8")
        PATCH(self.target)
        self.patched = self.target.read_text(encoding="utf-8")
        self.assertIn("DRNAS_MEASURE_INITGUI_EAGER", self.patched)
        self.assertIn("if _drnas_measure_os.environ.get(", self.patched)
        compile(self.patched, str(self.target), "exec")

    def run_init(self, allowed):
        imported = []
        real_import = builtins.__import__

        def audit_import(name, *args, **kwargs):
            if name in ("Measure", "MeasureGui"):
                imported.append(name)
                return types.ModuleType(name)
            return real_import(name, *args, **kwargs)

        globals_ = {"__builtins__": dict(vars(builtins), __import__=audit_import)}
        with patch.dict(os.environ, {"DRNAS_MEASURE_INITGUI_EAGER": allowed}):
            exec(compile(self.patched, str(self.target), "exec"), globals_)
        return imported

    def test_default_blocks_early_imports(self):
        self.assertEqual(self.run_init("0"), [])

    def test_explicit_override_restores_upstream_sequence(self):
        self.assertEqual(self.run_init("1"), ["Measure", "MeasureGui"])

    def test_source_comments_are_retained(self):
        self.assertIn("# upstream FreeCAD startup note", self.patched)

    def test_reject_changed_upstream_contract(self):
        self.target.write_text("import Measure\n", encoding="utf-8")
        with self.assertRaises(SystemExit):
            PATCH(self.target)


if __name__ == "__main__":
    unittest.main()
