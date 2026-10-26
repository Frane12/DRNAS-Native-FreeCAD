"""Host checks for the native-serialization/Android-provider handshake."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class AndroidSaveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.doc = types.SimpleNamespace(Name='Model', Label='../Čudni / prozor', FileName='')
        self.gui = types.SimpleNamespace(Modified=True)
        self.direct = True
        self.thumbnail = True
        self.app = types.ModuleType('FreeCAD')
        self.app.ActiveDocument = self.doc
        self.app.getDocument = lambda name: self.doc
        self.app.addDocumentObserver = lambda observer: None
        def parameter(path):
            attribute = 'thumbnail' if path.endswith('/Document') else 'direct'
            return types.SimpleNamespace(
                GetBool=lambda name, default: getattr(self, attribute),
                SetBool=lambda name, value: setattr(self, attribute, value))
        self.app.ParamGet = parameter
        self.gui_module = types.ModuleType('FreeCADGui')
        self.gui_module.getDocument = lambda name: self.gui
        self.gui_module.addDocumentObserver = lambda observer: None
        self.marks = []
        runtime = types.ModuleType('drnas_android_runtime')
        runtime._drnas_mark = self.marks.append
        def save_copy(path):
            self.assertFalse(self.direct)
            self.assertFalse(self.thumbnail)
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('Document.xml', '<Document label="Čudni prozor"/>')
                archive.writestr('PartShape.brp', b'representative payload')
        self.doc.saveCopy = save_copy
        modules = patch.dict(sys.modules, {'FreeCAD': self.app, 'FreeCADGui': self.gui_module,
                                          'drnas_android_runtime': runtime})
        modules.start()
        self.addCleanup(modules.stop)
        path = ROOT / 'full-gui/integrated-apk/drnas_android_save.py'
        spec = importlib.util.spec_from_file_location('save_runtime', path)
        self.save = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.save)

    def prepare(self, name='request'):
        return self.save.prepare_save(self.root / name)

    def test_snapshot_is_valid_and_durable_but_does_not_clear_modified(self):
        info = self.prepare()
        self.assertEqual(info, json.loads((self.root / 'request/snapshot.json').read_text()))
        self.assertEqual(Path(info['path']).parent, self.root / 'request')
        self.assertTrue(info['filename'].endswith('.FCStd'))
        self.assertNotIn('/', info['filename'])
        with zipfile.ZipFile(info['path']) as archive:
            self.assertIn('Document.xml', archive.namelist())
        self.assertTrue(self.gui.Modified)
        self.assertTrue(self.direct)
        self.assertTrue(self.thumbnail)
        self.assertEqual(self.doc.FileName, '')
        self.assertTrue(self.save.finish_save(info['token'], True))
        self.assertFalse(self.gui.Modified)

    def test_cancel_and_provider_error_preserve_dirty_state_and_copy(self):
        for name in ('cancel', 'provider-failure'):
            info = self.prepare(name)
            self.assertFalse(self.save.finish_save(info['token'], False))
            self.assertTrue(self.gui.Modified)
            self.assertTrue(Path(info['path']).is_file())

    def test_edit_during_transfer_is_not_marked_saved(self):
        info = self.prepare()
        self.save._observer.changed(types.SimpleNamespace(Document=self.doc), 'Shape')
        self.assertFalse(self.save.finish_save(info['token'], True))
        self.assertTrue(self.gui.Modified)

    def test_closed_document_does_not_clear_another_document_with_same_name(self):
        info = self.prepare()
        self.app.getDocument = lambda name: types.SimpleNamespace(Name='Model')
        self.assertFalse(self.save.finish_save(info['token'], True))
        self.assertTrue(self.gui.Modified)

    def test_save_copy_keeps_edits_dirty_and_reuses_session_destination_key(self):
        first = self.prepare('first')
        second = self.prepare('second')
        self.assertEqual(first['key'], second['key'])
        self.assertNotEqual(first['token'], second['token'])
        self.assertNotEqual(first['path'], second['path'])
        self.assertFalse(self.save.finish_save(first['token'], True, mark_clean=False))
        self.assertTrue(self.gui.Modified)

    def test_native_failure_and_invalid_zip_restore_preference_and_dirty_state(self):
        def fail(path):
            raise RuntimeError('BRep write failed')
        self.doc.saveCopy = fail
        with self.assertRaisesRegex(RuntimeError, 'BRep write failed'):
            self.prepare('native-failure')
        self.assertTrue(self.direct)
        self.assertTrue(self.thumbnail)
        self.assertTrue(self.gui.Modified)
        self.doc.saveCopy = lambda path: Path(path).write_text('not a zip')
        with self.assertRaises(zipfile.BadZipFile):
            self.prepare('invalid-zip')
        self.assertTrue(self.direct)
        self.assertTrue(self.thumbnail)
        self.assertTrue(self.gui.Modified)

    def test_missing_document_does_not_start_save(self):
        self.app.ActiveDocument = None
        with self.assertRaisesRegex(RuntimeError, 'Nema aktivnog dokumenta'):
            self.prepare()
        self.assertFalse((self.root / 'request').exists())


if __name__ == '__main__':
    unittest.main()
