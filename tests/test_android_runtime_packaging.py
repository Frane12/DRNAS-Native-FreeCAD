"""Host regression checks; these do not claim Android/FreeCAD runtime validation."""
import importlib.util
import os
from pathlib import Path
import runpy
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'full-gui/integrated-apk/stage-freecad-assets.py'
RUNTIME = SCRIPT.with_name('drnas_android_runtime.py')


class AndroidPackagingTests(unittest.TestCase):
    def test_same_name_payload_update_invalidates_manifest(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source, bundle, native = (root / name for name in ('source', 'bundle', 'native'))
            native.mkdir()
            spec = importlib.util.spec_from_file_location('stage', SCRIPT)
            stage = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(stage)
            for name in stage.REQUIRED | {f'Mod/Part/test{i}.py' for i in range(25)}:
                file = source / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text('# original\n')
            for module in ('Part', 'PartGui', 'Sketcher', 'SketcherGui', 'Materials', 'MatGui', '_PartDesign', 'PartDesignGui'):
                (native / (module + '.so')).write_bytes(b'x' * 8192)
            with patch.object(sys, 'argv', [str(SCRIPT), str(source), str(bundle), str(native)]):
                stage.main()
                before = (bundle / 'freecad-manifest.txt').read_text()
                (source / 'Mod/Part/InitGui.py').write_text('# changed\n')
                stage.main()
                after = (bundle / 'freecad-manifest.txt').read_text()
            self.assertNotEqual(before, after)
            self.assertIn('lib/drnas_android_runtime.py', after)

    def test_storage_probe_and_missing_dependency_are_journaled(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / 'Mod/Part').mkdir(parents=True)
            app = types.ModuleType('FreeCAD')
            app.Console = types.SimpleNamespace(PrintError=lambda text: None, PrintWarning=lambda text: None)
            app.getUserCachePath = lambda: str(root / 'cache')
            app.getTempPath = lambda: str(root / 'temp')
            app.getResourceDir = lambda: str(root / 'share')
            for relative in ('Materials/Appearance/Default.FCMat',
                             'Materials/Standard/Default.FCMat',
                             'Models/Rendering/BasicRendering.yml'):
                file = root / 'share/Mod/Material/Resources' / relative
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text('original resource fixture')
            materials = types.ModuleType('Materials')
            requested = []
            materials.MaterialManager = lambda: types.SimpleNamespace(
                getMaterial=lambda uuid: requested.append(uuid))
            log = root / 'runtime.log'
            env = {'DRNAS_FREECAD_HOME': str(root), 'DRNAS_WB_LOG_PATH': str(log)}
            old_out, old_err, old_path = sys.stdout, sys.stderr, sys.path[:]
            try:
                with patch.dict(sys.modules, {'FreeCAD': app, 'Materials': materials}), patch.dict(os.environ, env), \
                     patch('faulthandler.enable'), patch('importlib.util.find_spec', return_value=None):
                    runtime = runpy.run_path(str(RUNTIME))
                    app.Console.PrintError('missing binding test')
                    self.assertIn('STORAGE PASS cache', log.read_text())
                    self.assertIn('MATERIAL PASS', log.read_text())
                    self.assertEqual(requested, ['5dbb7be6-8b63-479b-ab4c-87be02ead973'])
                    self.assertIn('PrintError missing binding test', log.read_text())
                    self.assertIn(str(root / 'Mod/Part'), sys.path)
                    self.assertEqual(list((root / 'cache').iterdir()), [])
                    blocked = root / 'file-not-directory'
                    blocked.write_text('keep')
                    app.getTempPath = lambda: str(blocked)
                    with self.assertRaises(FileExistsError):
                        runtime['_drnas_storage_test']()
                    self.assertIn('STORAGE FAIL temp', log.read_text())
                    self.assertEqual(blocked.read_text(), 'keep')
                    runtime['_drnas_fault_file'].close()
            finally:
                sys.stdout, sys.stderr, sys.path = old_out, old_err, old_path


if __name__ == '__main__':
    unittest.main()
