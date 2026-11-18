"""Regression: Android Python bindings must not downgrade native GLES Coin."""
import ast
from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'full-gui/integrated-apk/integrate-bindings.py'

class CoinBindingMergeTest(unittest.TestCase):
    def test_native_coin_never_replaced(self):
        syntax = ast.parse(SCRIPT.read_text())
        functions = [node for node in syntax.body if isinstance(node, ast.FunctionDef)
                     and node.name == 'may_replace_native_so']
        self.assertEqual(len(functions), 1)
        namespace = {}
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(SCRIPT), 'exec'), namespace)
        policy = namespace['may_replace_native_so']
        self.assertFalse(policy('libCoin.so'))
        for name in ('libFreeCADBase.so', 'libshiboken6.abi3.so', 'libGL.so'):
            self.assertTrue(policy(name))

    def test_requires_coin_provenance_and_pivy_abi(self):
        text = SCRIPT.read_text()
        for marker in ('binding_used - native_coin', 'original_coin_sha256',
                       'COIN_GLES_GUARD_V2', 'drnas_coin_android_gles_guard_v2',
                       'if not may_replace_native_so(path.name):'):
            self.assertIn(marker, text)

if __name__ == '__main__':
    unittest.main()
