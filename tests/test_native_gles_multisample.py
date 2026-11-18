"""Compile the patched native Qt query/restore path against instrumented contexts."""
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

PATCH = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                          'full-gui/upstream/patch-document-diagnostics.py'))
ORIGINAL = '''    QOpenGLFunctions* functions = w->context() ? w->context()->functions() : nullptr;
    const bool multisampleEnabled = functions && functions->glIsEnabled(GL_MULTISAMPLE) == GL_TRUE;
    paint();
    if (functions) {
        if (multisampleEnabled) functions->glEnable(GL_MULTISAMPLE);
        else functions->glDisable(GL_MULTISAMPLE);
    }
'''


class NativeGlesMultisampleTest(unittest.TestCase):
    def test_gles_skips_invalid_queries_desktop_restores_state(self):
        patched = PATCH['patch_native_multisample'](ORIGINAL)
        source = r'''
#include <cassert>
constexpr int GL_MULTISAMPLE=0x809d, GL_TRUE=1;
struct QOpenGLFunctions {
    bool state=false;
    int queries=0, enables=0, disables=0;
    int glIsEnabled(int) { ++queries; return state; }
    void glEnable(int) { ++enables; state=true; }
    void glDisable(int) { ++disables; state=false; }
};
struct QOpenGLContext {
    bool es;
    QOpenGLFunctions gl;
    bool isOpenGLES() { return es; }
    QOpenGLFunctions* functions() { return &gl; }
};
struct Widget {
    QOpenGLContext* ctx;
    QOpenGLContext* context() { return ctx; }
};
int paints=0;
void paint() { ++paints; }
void exercise(Widget* w) {
''' + patched + r'''
}
int main() {
    for (bool initial : {false, true}) {
        QOpenGLContext es{true, {initial}};
        Widget w{&es}; exercise(&w);
        assert(es.gl.queries==0 && es.gl.enables==0 && es.gl.disables==0);
        assert(es.gl.state==initial);
        QOpenGLContext desktop{false, {initial}};
        w.ctx=&desktop; exercise(&w);
        assert(desktop.gl.queries==1 && desktop.gl.state==initial);
        assert(desktop.gl.enables==int(initial));
        assert(desktop.gl.disables==int(!initial));
    }
    Widget absent{nullptr}; exercise(&absent);
    assert(paints==5);
}
'''
        source = '#include <initializer_list>\n' + source
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'test.cpp'
            path.write_text(source)
            binary = Path(temp) / 'test'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_source_drift_fails(self):
        with self.assertRaises(RuntimeError):
            PATCH['patch_native_multisample']('changed source')


if __name__ == '__main__':
    unittest.main()
