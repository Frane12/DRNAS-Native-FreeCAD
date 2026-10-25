"""Test deferred framing without loading Android/FreeCAD or replacing CAD data."""
import ast
from pathlib import Path
import sys
import os
import tempfile
import traceback
from types import SimpleNamespace as NS

source = Path(__file__).parents[2] / "integrated-apk/drnas_android_runtime.py"
tree = ast.parse(source.read_text())
function = next(node for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == "_drnas_mobile_view_tick")
calls = []
marks = []
coin = NS(__file__='genuine-pivy-test-import')
sys.modules['pivy'] = NS(coin=coin)
loader = next(node for node in tree.body
              if isinstance(node, ast.FunctionDef) and node.name == '_drnas_load_coin_bindings')
def camera():
    assert scope['_drnas_coin_module'] is coin, 'Coin SWIG registry must be loaded BEFORE wrapping the camera'
    calls.append('bridge')
    return NS(getTypeId=lambda: NS(getName=lambda: 'OrthographicCamera'))
view = NS(viewAxonometric=lambda: calls.append("iso"),
          fitAll=lambda: calls.append("fit"), redraw=lambda: calls.append("redraw"),
          getCameraNode=camera, getSceneGraph=lambda: NS(getNumChildren=lambda: 1))
gui = NS(activeDocument=lambda: NS(activeView=lambda: view))
sys.modules["FreeCADGui"] = gui
doc = NS(Name="Unnamed", Objects=[])
app = NS(ActiveDocument=doc)
scope = {"App": app, "_drnas_framed_documents": {}, "_drnas_mark": marks.append,
         'sys': sys, 'traceback': traceback, '_drnas_coin_module': None}
exec(compile(ast.Module(body=[loader], type_ignores=[]), str(source), 'exec'), scope)
exec(compile(ast.Module(body=[function], type_ignores=[]), str(source), "exec"), scope)
tick = scope[function.name]
tick()
assert calls == [], "empty documents must not move the camera"
helpers = [NS(Name=name, isDerivedFrom=lambda _: False,
              ViewObject=NS(Visibility=True), Shape=NS(isNull=lambda: False))
           for name in ('X_Axis', 'XY_Plane', 'Origin001', 'LCS')]
doc.Objects.extend(helpers)
tick()
assert calls == [] and not scope['_drnas_framed_documents'], 'origin and datum geometry must not consume first-BRep framing'
doc.Objects.clear()
shape = NS(Name="Sphere", TypeId='Part::Sphere', isDerivedFrom=lambda kind: kind=='Part::Feature',
           ViewObject=NS(Visibility=True), Shape=NS(isNull=lambda: False))
doc.Objects.append(shape)
tick()
assert calls == ['bridge', "iso", "fit", "redraw"], "first actual BRep must be framed after bridge initialization"
assert any('COIN_BRIDGE PASS' in mark for mark in marks)
tick()
assert calls.count("fit") == 1, "later edits must preserve the user's camera"
assert shape.ViewObject.Visibility and doc.Objects == [shape], "framing must preserve CAD data"
doc.Objects.clear()
tick()
app.ActiveDocument = NS(Name="Unnamed", Objects=[shape])
tick()
assert calls.count("fit") == 2, "a new document reusing the same name must be framed"
def broken_camera():
    raise RuntimeError('SWIG incompatibility test')
view.getCameraNode = broken_camera
app.ActiveDocument = NS(Name='BrokenBridge', Objects=[shape])
tick()
assert calls.count('fit') == 3, 'SWIG failure must not skip native framing'
assert any('COIN_BRIDGE FAIL' in mark and 'SWIG incompatibility test' in mark for mark in marks)
assert sum('COIN_BINDINGS loaded=' in mark for mark in marks) == 1, 'Pivy must load once'
app.ActiveDocument = None
tick()
watch = next(node for node in tree.body
             if isinstance(node, ast.FunctionDef) and node.name == "_drnas_render_watch")
scope.update(os=os, _drnas_render_observer=None)
app.addDocumentObserver = lambda observer: None
gui.addDocumentObserver = lambda observer: None
exec(compile(ast.Module(body=[watch], type_ignores=[]), str(source), "exec"), scope)
with tempfile.TemporaryDirectory() as directory:
    scope[watch.name](directory)
    observer = scope['_drnas_render_observer']
    for deleted in (doc, NS(Document=doc)):
        scope['_drnas_framed_documents'][doc.Name] = doc
        observer.slotDeletedDocument(deleted)
        assert doc.Name not in scope['_drnas_framed_documents'], "App and Gui close notifications must both clear framing state"
print("Deferred first-shape framing checks passed")

