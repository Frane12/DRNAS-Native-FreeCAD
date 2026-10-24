"""Check model transaction boundaries without loading Android native libraries."""
import ast
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import Mock, patch

source = Path(__file__).parents[1] / 'integrated-apk/drnas_android_runtime.py'
node = next(n for n in ast.parse(source.read_text()).body
            if isinstance(n, ast.FunctionDef) and n.name == '_drnas_model_command')


class ModelCommands(unittest.TestCase):
    def setUp(self):
        self.shape = SimpleNamespace(isNull=lambda: False, isValid=lambda: True)
        self.result = SimpleNamespace(Shape=self.shape, ViewObject=SimpleNamespace(Visibility=True))
        self.doc = Mock()
        self.doc.addObject.return_value = self.result
        self.gui = SimpleNamespace(Selection=Mock())
        self.gui.Selection.getSelection.return_value = []
        self.app = SimpleNamespace(ActiveDocument=self.doc)
        self.observer = Mock()
        self.ns = dict(App=self.app, _drnas_mark=Mock(), _drnas_render_observer=self.observer)
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), 'exec'), self.ns)
        self.modules = patch.dict(sys.modules, FreeCADGui=self.gui)
        self.modules.start()
        self.addCleanup(self.modules.stop)

    def command(self, action, value=None):
        return self.ns['_drnas_model_command'](action, value)

    def test_box_commits_and_selects_actual_object(self):
        self.command('box')
        self.doc.addObject.assert_called_once_with('Part::Box', 'Box')
        self.doc.recompute.assert_called_once()
        self.doc.commitTransaction.assert_called_once()
        self.doc.abortTransaction.assert_not_called()
        self.gui.Selection.addSelection.assert_called_once_with(self.result)

    def test_invalid_shape_rolls_back(self):
        self.shape.isValid = lambda: False
        with self.assertRaises(RuntimeError):
            self.command('box')
        self.doc.abortTransaction.assert_called_once()
        self.doc.commitTransaction.assert_not_called()

    def test_resize_rejects_missing_selection_before_transaction(self):
        with self.assertRaises(ValueError):
            self.command('resize', [10, 20, 30])
        self.doc.openTransaction.assert_not_called()

    def test_resize_rejects_nan_before_transaction(self):
        self.gui.Selection.getSelection.return_value = [SimpleNamespace(
            Document=self.doc, isDerivedFrom=lambda kind: kind == 'Part::Box')]
        with self.assertRaises(ValueError):
            self.command('resize', [float('nan'), 20, 30])
        self.doc.openTransaction.assert_not_called()

    def test_undo_uses_freecad_history_and_notifies_renderer(self):
        self.command('undo')
        self.doc.undo.assert_called_once()
        self.doc.recompute.assert_called_once()
        self.observer.dirty.assert_called_once()

    def test_stale_selection_is_rejected(self):
        self.doc.getObject.return_value = None
        with self.assertRaises(ValueError):
            self.command('select', 'DeletedBox')
        self.gui.Selection.addSelection.assert_not_called()


if __name__ == '__main__':
    unittest.main()
