"""Geometry bridge contracts; no claim of Android GPU validation."""
import ast
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'full-gui/integrated-apk/drnas_android_runtime.py'


class Placement:
    def __init__(self, x=0):
        self.x = x
    def multiply(self, other):
        return Placement(self.x + other.x)
    def inverse(self):
        return Placement(-self.x)


class Shape:
    def __init__(self, x=0):
        self.Placement = Placement(x)
    def isNull(self):
        return False
    def copy(self):
        return Shape(self.Placement.x)


class RenderSnapshotTests(unittest.TestCase):
    def export(self, directory, objects, name='Document'):
        tree = ast.parse(SCRIPT.read_text())
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                        and node.name == '_drnas_render_export')
        app = types.SimpleNamespace(ActiveDocument=types.SimpleNamespace(Objects=objects, Name=name))
        captured = []
        def compound(shapes):
            captured.extend(shapes)
            return types.SimpleNamespace(exportBrep=lambda path: Path(path).write_text('real-shape-fixture'))
        env = {'App': app, 'os': __import__('os'), '_drnas_mark': lambda message: None}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(SCRIPT), 'exec'), env)
        with patch.dict(sys.modules, {'Part': types.SimpleNamespace(makeCompound=compound)}):
            env['_drnas_render_export'](str(directory))
        return captured, json.loads((directory / 'snapshot.json').read_text())

    def obj(self, name, local=0, global_x=0, visible=True, parents=(), body=False):
        return types.SimpleNamespace(
            Name=name, Shape=Shape(local), Placement=Placement(local),
            getGlobalPlacement=lambda: Placement(global_x), InListRecursive=list(parents),
            ViewObject=types.SimpleNamespace(Visibility=visible),
            isDerivedFrom=lambda kind: body and kind == 'PartDesign::Body')

    def test_parent_placement_applied_once_and_document_not_mutated(self):
        obj = self.obj('Box', local=10, global_x=110)
        with tempfile.TemporaryDirectory() as td:
            shapes, info = self.export(Path(td), [obj])
            self.assertEqual(shapes[0].Placement.x, 110)
            self.assertEqual(obj.Shape.Placement.x, 10)
            self.assertEqual(info['objects'], ['Box'])

    def test_hidden_body_and_duplicate_body_feature_not_exported(self):
        body = self.obj('Body', body=True)
        tip = self.obj('Pad', parents=[body])
        with tempfile.TemporaryDirectory() as td:
            shapes, info = self.export(Path(td), [body, tip])
            self.assertEqual(info['objects'], ['Body'])
            body.ViewObject.Visibility = False
            shapes, info = self.export(Path(td), [body, tip])
            self.assertEqual(info['count'], 0)
            self.assertFalse((Path(td) / 'document.brep').exists())

    def test_visible_boolean_input_is_not_silently_removed(self):
        cut = self.obj('Cut')
        box = self.obj('Box', parents=[cut])
        with tempfile.TemporaryDirectory() as td:
            _, info = self.export(Path(td), [cut, box])
            self.assertEqual(info['objects'], ['Cut', 'Box'])

    def test_empty_document_removes_previous_snapshot(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            self.export(root, [self.obj('Box')])
            self.assertTrue((root / 'document.brep').exists())
            _, info = self.export(root, [], name='Empty')
            self.assertFalse((root / 'document.brep').exists())
            self.assertEqual(info, {'document': 'Empty', 'objects': [], 'count': 0})


if __name__ == '__main__':
    unittest.main()
