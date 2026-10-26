"""Host checks for restore paths and BRep preference lifetime; no device claims."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / 'full-gui/integrated-apk/drnas_android_runtime.py'


class DocumentOpenTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for module, names in {'BIM': ('ArchMaterial', 'ArchWall', 'ArchWindow'),
                              'Draft': ('Draft',)}.items():
            directory = self.root / 'Mod' / module
            directory.mkdir(parents=True)
            for name in names:
                (directory / (name + '.py')).write_text('raise AssertionError("must stay lazy")\n')
        self.app = types.ModuleType('FreeCAD')
        self.app.__drnas_runtime_installed__ = True
        self.app.__ModDirs__ = ['/original/workbench']
        self.value = True
        self.history = []
        def set_bool(name, value):
            self.value = value
            self.history.append((name, value))
        self.app.ParamGet = lambda name: types.SimpleNamespace(
            GetBool=lambda key, default: self.value, SetBool=set_bool)
        self.path_before = sys.path[:]
        self.addCleanup(lambda: sys.path.__setitem__(slice(None), self.path_before))
        self.modules = patch.dict(sys.modules, {'FreeCAD': self.app})
        self.modules.start()
        self.addCleanup(self.modules.stop)
        self.env = patch.dict(os.environ, {'DRNAS_FREECAD_HOME': str(self.root),
                                          'DRNAS_WB_LOG_PATH': str(self.root / 'journal')})
        self.env.start()
        self.addCleanup(self.env.stop)
        spec = importlib.util.spec_from_file_location('document_open_runtime', RUNTIME)
        self.runtime = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.runtime)

    def test_paths_registered_lazily_without_allowing_document_directory(self):
        self.runtime._drnas_prepare_document_restore()
        self.runtime._drnas_prepare_document_restore()
        self.assertEqual(self.app.__ModDirs__, ['/original/workbench',
            str(self.root / 'Mod/Draft'), str(self.root / 'Mod/BIM')])
        self.assertNotIn(str(self.root), self.app.__ModDirs__)
        for name in ('Draft', 'ArchMaterial', 'ArchWall', 'ArchWindow'):
            self.assertNotIn(name, sys.modules)
        self.assertIn('RESTORE MODULE READY ArchWindow', (self.root / 'journal').read_text())

    def test_success_uses_temp_brep_and_restores_preference(self):
        expected = types.SimpleNamespace(Name='Window_Type')
        path = str(self.root / "prozor 'č'.FCStd")
        def open_document(actual):
            self.assertEqual(actual, path)
            self.assertFalse(self.value)
            self.assertIn(str(self.root / 'Mod/BIM'), self.app.__ModDirs__)
            return expected
        self.app.openDocument = open_document
        self.assertIs(self.runtime._drnas_open_android_document(path), expected)
        self.assertTrue(self.value)
        self.assertEqual(self.history, [('DirectAccess', False), ('DirectAccess', True)])

    def test_failed_open_restores_both_existing_preference_values(self):
        def fail(path):
            self.assertFalse(self.value)
            raise RuntimeError('native document error')
        self.app.openDocument = fail
        for previous in (True, False):
            self.value = previous
            with self.assertRaisesRegex(RuntimeError, 'native document error'):
                self.runtime._drnas_open_android_document('broken.FCStd')
            self.assertIs(self.value, previous)
        self.assertIn('ANDROID_OPEN FAILED', (self.root / 'journal').read_text())

    def test_foreign_module_origin_prevents_open(self):
        foreign = self.root / 'selected-document-directory'
        foreign.mkdir()
        (foreign / 'ArchWall.py').write_text('raise AssertionError("foreign module")\n')
        sys.path.insert(0, str(foreign))
        self.app.openDocument = lambda path: self.fail('open must not run')
        with self.assertRaisesRegex(ImportError, 'not from bundled BIM/Draft: ArchWall'):
            self.runtime._drnas_open_android_document('model.FCStd')
        self.assertNotIn(str(foreign), self.app.__ModDirs__)
        self.assertEqual(self.history, [])


if __name__ == '__main__':
    unittest.main()
