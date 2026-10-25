"""Regression tests for DRNAS's Android SAF -> embedded Python file-open path."""
import ast
import base64
import re
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

CPP = Path(__file__).resolve().parents[1] / "full-gui/qt-activity/IntegratedGui.cpp"


def open_script_template():
    code = CPP.read_text(encoding="utf-8")
    match = re.search(
        r"const QString script = QStringLiteral\(\s*(.*?)\s*\)"
        r"\.arg\(QString::fromLatin1\(staged\.toUtf8\(\)\.toBase64\(\)\)\);",
        code,
        re.S,
    )
    if match is None:
        raise AssertionError("Android Open Python script not found")
    parts = re.findall(r'^\s*("(?:[^"\\]|\\.)*")\s*$', match.group(1), re.M)
    if not parts:
        raise AssertionError("Could not decode C++ Python string literals")
    return "".join(ast.literal_eval(part) for part in parts)


class AndroidOpenPathTest(unittest.TestCase):
    def setUp(self):
        self.template = open_script_template()
        self.assertIn("base64.b64decode('%1', validate=True)", self.template)
        self.assertIn("os.path.isfile(_drnas_async_path)", self.template)

    def execute(self, payload):
        script = self.template.replace("%1", payload)
        compile(script, "<DRNAS Android Open>", "exec")
        self.opened = []
        self.markers = []
        gui = types.ModuleType("FreeCADGui")
        gui.open = lambda path: self.opened.append(path)
        with patch.dict(sys.modules, {"FreeCADGui": gui}):
            exec(script, {"_drnas_mark": self.markers.append})
        return self.opened

    def test_unicode_spaces_quotes_and_percent(self):
        with tempfile.TemporaryDirectory() as temp:
            for filename in ("kocka.FCStd", "moj model O'Neil.FCStd",
                             "čelična kugla 100%.FCStd", 'test "navodnik".stl'):
                with self.subTest(filename=filename):
                    path = Path(temp) / filename
                    path.write_bytes(b"freecad-path-smoke")
                    encoded = base64.b64encode(str(path).encode("utf-8")).decode("ascii")
                    self.assertEqual(self.execute(encoded), [str(path)])
                    self.assertTrue(any("PY_PATH_VALID" in m for m in self.markers))
                    self.assertTrue(any("PY_IMPORT_END" in m for m in self.markers))

    def test_missing_path_rejected_before_freecad_open(self):
        with tempfile.TemporaryDirectory() as temp:
            missing = str(Path(temp) / "missing.FCStd")
            encoded = base64.b64encode(missing.encode("utf-8")).decode("ascii")
            with self.assertRaises(FileNotFoundError):
                self.execute(encoded)
            self.assertFalse(self.opened)
            self.assertTrue(any("PY_IMPORT_EXCEPTION" in m for m in self.markers))

    def test_malformed_base64_logged(self):
        with self.assertRaises(Exception):
            self.execute("not-base64!??")
        self.assertFalse(self.opened)
        self.assertTrue(any("PY_IMPORT_EXCEPTION" in m for m in self.markers))


if __name__ == "__main__":
    unittest.main()
