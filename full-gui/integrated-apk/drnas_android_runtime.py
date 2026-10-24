"""Android integration diagnostics; never substitute FreeCAD commands or Qt bindings."""
import os
import sys
import tempfile
import traceback
import importlib
import importlib.util
import faulthandler
import FreeCAD as App

# Keep exception reporting deterministic on Android terminals without TTY.
os.environ.setdefault('PYTHON_COLORS', '0')
os.environ.setdefault('NO_COLOR', '1')
try:
    import sys as _drnas_sys
    if hasattr(_drnas_sys, 'exceptionhook'):
        _drnas_sys.excepthook = _drnas_sys.__excepthook__
except Exception:
    pass


def _drnas_mark(message):
    path = os.environ.get('DRNAS_WB_LOG_PATH')
    if path:
        with open(path, 'a', encoding='utf-8') as stream:
            stream.write('[ANDROID_RUNTIME] ' + str(message) + '\n')
            stream.flush()


class _DrnasTee:
    def __init__(self, original, label):
        self.original, self.label = original, label

    def write(self, text):
        if text.strip():
            _drnas_mark(self.label + ' ' + text.rstrip())
        if self.original is not None:
            self.original.write(text)
        return len(text)

    def flush(self):
        if self.original is not None:
            self.original.flush()

    def __getattr__(self, name):
        return getattr(self.original, name)


def _drnas_storage_test():
    # Exercise the actual cache selected by FreeCAD, not just Android's default.
    for label, path in [('cache', App.getUserCachePath()),
                        ('temp', App.getTempPath())]:
        _drnas_mark('STORAGE BEGIN ' + label + '=' + repr(path))
        try:
            os.makedirs(path, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix='DRNAS-probe-', dir=path) as directory:
                first = os.path.join(directory, 'probe.bin')
                renamed = os.path.join(directory, 'renamed.bin')
                with open(first, 'xb') as stream:
                    stream.write(b'DRNAS storage probe\n')
                    stream.flush()
                    os.fsync(stream.fileno())
                os.rename(first, renamed)
                with open(renamed, 'rb') as stream:
                    assert stream.read() == b'DRNAS storage probe\n'
                os.unlink(renamed)
            _drnas_mark('STORAGE PASS ' + label + ' mkdir/write/fsync/rename/read/remove')
        except Exception:
            _drnas_mark('STORAGE FAIL ' + label + '\n' + traceback.format_exc())
            raise



def _drnas_material_test():
    # Use the real manager, not a placeholder material or suppressed exception.
    root = os.path.join(App.getResourceDir(), 'Mod', 'Material', 'Resources')
    _drnas_mark('MATERIAL BEGIN resource=' + repr(root))
    try:
        for relative in ('Materials/Appearance/Default.FCMat',
                         'Materials/Standard/Default.FCMat',
                         'Models/Rendering/BasicRendering.yml'):
            path = os.path.join(root, relative)
            if not os.path.isfile(path):
                raise FileNotFoundError(path)
        import Materials
        manager = Materials.MaterialManager()
        manager.getMaterial('5dbb7be6-8b63-479b-ab4c-87be02ead973')
        _drnas_mark('MATERIAL PASS genuine default appearance resolved by UUID')
    except Exception:
        _drnas_mark('MATERIAL FAIL\\n' + traceback.format_exc())
        raise


def _drnas_part_commands():
    import FreeCADGui as Gui
    # These are the original imports from the pinned Part/InitGui.py. Its
    # Initialize catches failures internally, so capture each traceback here.
    for name in ('BasicShapes.CommandShapes', 'CompoundTools._CommandCompoundFilter',
                 'CompoundTools._CommandExplodeCompound', 'BOPTools'):
        _drnas_mark('COMMAND IMPORT BEGIN ' + name)
        try:
            module = importlib.import_module(name)
            if name == 'BOPTools':
                module.importAll()
                module.addCommands()
                import PartGui
                PartGui.BOPTools = module
            _drnas_mark('COMMAND IMPORT PASS ' + name + ' file=' + str(module.__file__))
        except Exception:
            _drnas_mark('COMMAND IMPORT FAIL ' + name + '\n' + traceback.format_exc())
    commands = set(Gui.listCommands())
    expected = ('Part_ToleranceSet', 'Part_CompoundFilter', 'Part_ExplodeCompound',
                'Part_XOR', 'Part_Slice', 'Part_SliceApart', 'Part_BooleanFragments',
                'Part_JoinCutout', 'Part_JoinEmbed', 'Part_JoinConnect', 'Part_Tube',
                'Std_MassProperties')
    _drnas_mark('COMMANDS missing=' + repr([name for name in expected if name not in commands]))
    _drnas_mark('ACTIVE WORKBENCH ' + Gui.activeWorkbench().name())


# Render Lab exports actual document BReps. No primitive substitutes and no
# changes to the document, ViewProviders, or saved material properties.
_drnas_render_observer = None
_drnas_framed_documents = {}


def _drnas_mobile_view_tick():
    """Frame the first real shape after the portrait viewport has settled."""
    import FreeCADGui as Gui
    doc = App.ActiveDocument
    gui_doc = Gui.activeDocument()
    if doc is None or gui_doc is None:
        return
    visible = [obj for obj in doc.Objects
               if getattr(obj, 'ViewObject', None) is not None
               and obj.ViewObject.Visibility and hasattr(obj, 'Shape')
               and not obj.Shape.isNull()]
    view = gui_doc.activeView()
    if not visible or not hasattr(view, 'fitAll'):
        return
    # Do this once per document, not on each camera gesture or recompute.
    # Preserve real ViewProviders, visibility, materials and subsequent views.
    if _drnas_framed_documents.get(doc.Name) is not doc:
        view.viewAxonometric()
        view.fitAll()
        _drnas_framed_documents[doc.Name] = doc
        _drnas_mark('PORTRAIT FIRST_SHAPE framed doc=' + doc.Name
                    + ' visible=' + repr([obj.Name for obj in visible]))
    view.redraw()


def _drnas_render_watch(directory):
    global _drnas_render_observer
    os.makedirs(directory, exist_ok=True)
    if _drnas_render_observer is not None:
        return

    class Observer:
        def dirty(self, *args):
            import time
            try:
                with open(os.path.join(directory, 'dirty'), 'w') as stream:
                    stream.write(str(time.monotonic_ns()))
            except OSError:
                # A failed preview notification must not interrupt CAD edits.
                pass
        slotChangedObject = dirty
        slotCreatedObject = dirty
        slotDeletedObject = dirty
        slotRecomputedDocument = dirty
        slotCreatedDocument = dirty
        slotActivateDocument = dirty

        def slotDeletedDocument(self, doc):
            _drnas_framed_documents.pop(doc.Name, None)
            self.dirty()

    _drnas_render_observer = Observer()
    App.addDocumentObserver(_drnas_render_observer)
    import FreeCADGui as Gui
    Gui.addDocumentObserver(_drnas_render_observer)
    _drnas_render_observer.dirty()


def _drnas_render_export(directory):
    import traceback
    try:
        import json
        import Part
        doc = App.ActiveDocument
        shapes, names = [], []
        if doc is not None:
            for obj in doc.Objects:
                view = getattr(obj, 'ViewObject', None)
                if view is None or not view.Visibility:
                    continue
                # A hidden parent suppresses its visible children in the real GUI.
                # Follow containers, not InListRecursive (which also contains
                # dependent Boolean features that do not control this visibility).
                parents = []
                parent = obj.getParentGeoFeatureGroup() or obj.getParentGroup()
                while parent is not None and parent not in parents:
                    parents.append(parent)
                    parent = parent.getParentGeoFeatureGroup() or parent.getParentGroup()
                if any((parent.isDerivedFrom('App::Part') or parent.isDerivedFrom('PartDesign::Body')) and
                       not parent.ViewObject.Visibility for parent in parents):
                    continue
                shape = getattr(obj, 'Shape', None)
                if shape is None or shape.isNull():
                    continue
                # A visible Body already includes its tip; do not duplicate that shape.
                if any(parent.isDerivedFrom('PartDesign::Body') and getattr(parent, 'ViewObject', None)
                       and parent.ViewObject.Visibility and hasattr(parent, 'Shape')
                       and not parent.Shape.isNull() for parent in parents):
                    continue
                shape = shape.copy()
                # Shape already carries the object's local placement. Add only
                # the ancestor placement, avoiding a second local transform.
                if hasattr(obj, 'getGlobalPlacement') and hasattr(obj, 'Placement'):
                    ancestors = obj.getGlobalPlacement().multiply(obj.Placement.inverse())
                    shape.Placement = ancestors.multiply(shape.Placement)
                shapes.append(shape)
                names.append(obj.Name)
        target = os.path.join(directory, 'document.brep')
        if shapes:
            temporary = target + '.tmp'
            Part.makeCompound(shapes).exportBrep(temporary)
            os.replace(temporary, target)
        elif os.path.exists(target):
            os.unlink(target)
        metadata = {'document': doc.Name if doc else None, 'objects': names,
                    'count': len(shapes)}
        with open(os.path.join(directory, 'snapshot.json'), 'w') as stream:
            json.dump(metadata, stream)
        _drnas_mark('RENDER_BASELINE exported ' + repr(metadata))
    except Exception:
        _drnas_mark("RENDER_BASELINE EXPORT_FAILED\n" + traceback.format_exc())
        raise


if not getattr(App, '__drnas_runtime_installed__', False):
    # Keep FreeCAD's original console delivery, while journaling swallowed
    # InitGui import errors that otherwise only reach the notification popup.
    def _drnas_console_tee(original, label):
        def emit(message):
            _drnas_mark(label + ' ' + str(message))
            return original(message)
        return emit
    for _drnas_console_name in ('PrintError', 'PrintWarning'):
        setattr(App.Console, _drnas_console_name,
                _drnas_console_tee(getattr(App.Console, _drnas_console_name), _drnas_console_name))
    sys.stdout = _DrnasTee(sys.stdout, 'stdout')
    sys.stderr = _DrnasTee(sys.stderr, 'stderr')
    _drnas_root = os.environ['DRNAS_FREECAD_HOME'].rstrip('/')
    # Legacy InitGui imports BasicShapes/BOPTools as top-level packages;
    # adding only Mod cannot resolve packages inside Mod/Part.
    for _drnas_relative in ('Mod', 'Mod/Part', 'Mod/Sketcher', 'Mod/PartDesign'):
        _drnas_directory = os.path.join(_drnas_root, _drnas_relative)
        if os.path.isdir(_drnas_directory) and _drnas_directory not in sys.path:
            sys.path.append(_drnas_directory)
    importlib.invalidate_caches()
    _drnas_mark('sys.path=' + repr(sys.path))
    # Observe module availability without importing native extensions or altering FreeCAD.
    for _drnas_name in ('Sketcher', 'SketcherGui', 'Part', 'PartGui'):
        try:
            _drnas_spec = importlib.util.find_spec(_drnas_name)
            _drnas_mark('MODULE SPEC ' + _drnas_name + '=' + repr(_drnas_spec))
        except BaseException as _drnas_exc:
            _drnas_mark('MODULE SPEC ERROR ' + _drnas_name + '=' + repr(_drnas_exc))
    _drnas_mark('APP INIT ERROR=' + str(getattr(App, '__DRNAS_AndroidInitError__', 'none recorded')))
    for _drnas_binding in ('PySide', 'PySide6', 'shiboken6', 'pivy'):
        try:
            _drnas_spec = importlib.util.find_spec(_drnas_binding)
            _drnas_mark('BINDING ' + _drnas_binding + '=' + repr(_drnas_spec))
        except Exception:
            _drnas_mark('BINDING ERROR ' + _drnas_binding + '\n' + traceback.format_exc())
    _drnas_fault_file = open(os.environ['DRNAS_WB_LOG_PATH'], 'a')
    faulthandler.enable(file=_drnas_fault_file, all_threads=True)
    App.__drnas_runtime_installed__ = True
    _drnas_storage_test()
    _drnas_material_test()
