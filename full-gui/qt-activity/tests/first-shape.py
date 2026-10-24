"""Test deferred framing without loading Android/FreeCAD or replacing CAD data."""
import ast
from pathlib import Path
import sys
from types import SimpleNamespace as NS

source = Path(__file__).parents[2] / "integrated-apk/drnas_android_runtime.py"
tree = ast.parse(source.read_text())
function = next(node for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == "_drnas_mobile_view_tick")
calls = []
view = NS(viewAxonometric=lambda: calls.append("iso"),
          fitAll=lambda: calls.append("fit"), redraw=lambda: calls.append("redraw"))
gui = NS(activeDocument=lambda: NS(activeView=lambda: view))
sys.modules["FreeCADGui"] = gui
doc = NS(Name="Unnamed", Objects=[])
app = NS(ActiveDocument=doc)
scope = {"App": app, "_drnas_framed_documents": {}, "_drnas_mark": lambda _: None}
exec(compile(ast.Module(body=[function], type_ignores=[]), str(source), "exec"), scope)
tick = scope[function.name]
tick()
assert calls == [], "empty documents must not move the camera"
shape = NS(Name="Sphere", ViewObject=NS(Visibility=True), Shape=NS(isNull=lambda: False))
doc.Objects.append(shape)
tick()
assert calls == ["iso", "fit", "redraw"], "first actual BRep must be framed once"
tick()
assert calls.count("fit") == 1, "later edits must preserve the user's camera"
assert shape.ViewObject.Visibility and doc.Objects == [shape], "framing must preserve CAD data"
doc.Objects.clear()
tick()
app.ActiveDocument = NS(Name="Unnamed", Objects=[shape])
tick()
assert calls.count("fit") == 2, "a new document reusing the same name must be framed"
app.ActiveDocument = None
tick()
print("Deferred first-shape framing checks passed")
