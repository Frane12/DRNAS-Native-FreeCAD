"""Regression test for diagnostic-only Draft initialization logging."""
import importlib.util
from pathlib import Path
import tempfile
import unittest


class DraftTracebackTest(unittest.TestCase):
    def test_retains_original_error_and_adds_traceback(self):
        script = (Path(__file__).resolve().parents[1] /
                  "full-gui/integrated-apk/stage-freecad-assets.py")
        spec = importlib.util.spec_from_file_location("drnas_stage_assets", script)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        # Use the exact upstream handler's indentation inside an initializer.
        original = (
            "def test_import():\n"
            "    if True:\n"
            "        try:\n"
            "            1 / 0\n"
            "        except Exception as exc:\n"
            "            FreeCAD.Console.PrintError(exc)\n"
        )
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "InitGui.py"
            source.write_text(original, encoding="utf-8")
            mod.instrument_draft_initgui(source)
            patched = source.read_text(encoding="utf-8")
            self.assertEqual(patched.count("[DRNAS_DRAFT_IMPORT_TRACEBACK]"), 1)
            entries = []

            class Console:
                @staticmethod
                def PrintError(message):
                    entries.append(str(message))

            FreeCAD = type("FakeFreeCAD", (), {"Console": Console})

            namespace = {"FreeCAD": FreeCAD}
            exec(compile(patched, str(source), "exec"), namespace)
            namespace["test_import"]()
            self.assertTrue(any("ZeroDivisionError" in e for e in entries))
            self.assertTrue(any("test_import" in e for e in entries))
            self.assertTrue(any("division by zero" in e for e in entries))


if __name__ == "__main__":
    unittest.main()
