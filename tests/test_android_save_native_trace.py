"""Exercise fatal Save diagnostics in a separate host process, without Qt."""
import os
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('g++'), 'Host C++ compiler unavailable')
class SaveNativeTraceTests(unittest.TestCase):
    def test_return_restores_handler_and_terminate_preserves_exception_detail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, binary = root / 'test.cpp', root / 'test'
            header = ROOT / 'full-gui/qt-activity/DrnasSaveNativeTrace.h'
            source.write_text(
                '#define DRNAS_SAVE_TRACE_HOST_TEST\n'
                '#include "' + str(header) + '"\n'
                '#include <stdexcept>\n'
                'int main(int argc,char**) {\n'
                ' auto before = std::get_terminate();\n'
                ' { DrnasSaveNativeTrace trace;\n'
                '   if(argc>1) throw std::runtime_error("native-save-test-error"); }\n'
                ' return std::get_terminate() == before ? 0 : 2; }\n')
            subprocess.run(['g++', '-std=c++17', str(source), '-ldl', '-o', str(binary)],
                           check=True, capture_output=True, timeout=30)
            normal = subprocess.run([str(binary)], capture_output=True, timeout=10)
            self.assertEqual(normal.returncode, 0)
            journal = root / 'journal.log'
            journal.write_text('')
            def no_core():
                resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            failed = subprocess.run([str(binary), 'fail'], capture_output=True, timeout=10,
                env=dict(os.environ, DRNAS_WB_LOG_PATH=str(journal)), preexec_fn=no_core)
            self.assertNotEqual(failed.returncode, 0)
            content = journal.read_text()
            self.assertIn('CXX_TERMINATE', content)
            self.assertIn('native-save-test-error', content)
            self.assertIn('FRAME', content)


if __name__ == '__main__':
    unittest.main()
