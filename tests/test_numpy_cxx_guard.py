"""Checks that the NumPy C++ ABI consolidation refuses unsafe input."""
import importlib.util
from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "full-gui/integrated-apk/consolidate-numpy-cxx.py"
spec = importlib.util.spec_from_file_location("drnas_cxx", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class NumPyCxxGuardTest(unittest.TestCase):
    def test_bad_main_lib_fails_closed(self):
        with self.assertRaises((RuntimeError, OSError)):
            module.consolidate(Path("/nonexistent/libc++_shared.so"),
                               Path("/nonexistent/Mod"), Path("/tmp/none.json"))

    def test_no_unsafe_patchelf_dependency(self):
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertNotIn('["patchelf"', source)
        self.assertIn("if left != right:", source)
        self.assertIn("changed = blob.replace", source)
        self.assertIn("len(changed) != len(blob)", source)


if __name__ == "__main__":
    unittest.main()
