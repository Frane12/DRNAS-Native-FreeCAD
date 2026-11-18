"""Host regression tests for four Coin3D desktop-only enums on Android GLES."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest

PATCH_PATH = Path(__file__).resolve().parents[1] / 'full-gui/upstream/patch-android-coin-gles.py'
spec = importlib.util.spec_from_file_location('coin_gles_patch', PATCH_PATH)
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)

GLUE = '''void applyOffset(int m, bool enable) {
  if (enable) {
      if (m & cc_glglue_LINES) glEnable(GL_POLYGON_OFFSET_LINE);
      else glDisable(GL_POLYGON_OFFSET_LINE);
      if (m & cc_glglue_POINTS) glEnable(GL_POLYGON_OFFSET_POINT);
      else glDisable(GL_POLYGON_OFFSET_POINT);
  } else {
      if (m & cc_glglue_LINES) glDisable(GL_POLYGON_OFFSET_LINE);
      if (m & cc_glglue_POINTS) glDisable(GL_POLYGON_OFFSET_POINT);
  }
}
'''
LINEWIDTH = '''void applyWidth() {
    GLfloat vals[2];
    glGetFloatv(GL_LINE_WIDTH_RANGE, vals);
}
'''
LAZY = '''struct FakeLazy {
  struct { int stipplenum = 0; } mutable glstate;
  mutable int cachebitmask = 0;
  bool colorindex = false;
  int diffuse = 0;
  void sendPackedDiffuse(unsigned) { diffuse++; }
  void sendTransparency(const int stipplenum) const {
  if (stipplenum == 0) {
    glDisable(GL_POLYGON_STIPPLE);
  }
  else {
    if (this->glstate.stipplenum <= 0) glEnable(GL_POLYGON_STIPPLE);
    glPolygonStipple(stipple_patterns[stipplenum]);
  }
  this->glstate.stipplenum = stipplenum;
  this->cachebitmask |= TRANSPARENCY_MASK;
  }
  void init() {
  glDisable(GL_POLYGON_STIPPLE);

  GLboolean rgba;
  glGetBooleanv(GL_RGBA_MODE, &rgba);
  if (!rgba) this->colorindex = TRUE;
  else {
    this->sendPackedDiffuse(0xccccccff);
  }
  }
};
'''
PREFIX = r'''
#include <cassert>
using GLboolean = unsigned char;
using GLfloat = float;
constexpr int GL_TRUE = 1, TRUE = 1, FALSE = 0;
constexpr int GL_POLYGON_OFFSET_LINE=10754, GL_POLYGON_OFFSET_POINT=10753;
constexpr int GL_POLYGON_STIPPLE=2882, GL_RGBA_MODE=3121;
constexpr int GL_LINE_WIDTH_RANGE=2850, GL_ALIASED_LINE_WIDTH_RANGE=0x846E;
constexpr int cc_glglue_LINES=1, cc_glglue_POINTS=2, TRANSPARENCY_MASK=4;
static int bad_calls=0;
static int valid_calls=0;
static unsigned char stipple_patterns[2][128] = {};
void glDisable(int cap) {
    if (cap==10753 || cap==10754 || cap==2882) ++bad_calls;
    else ++valid_calls;
}
void glEnable(int cap) {
    if (cap==10753 || cap==10754 || cap==2882) ++bad_calls;
    else ++valid_calls;
}
void glGetBooleanv(int cap, GLboolean* value) {
    if (cap==3121) ++bad_calls;
    *value=1;
}
void glPolygonStipple(const unsigned char*) {++bad_calls;}
void glGetFloatv(int pname, GLfloat* vals) {
    if (pname==GL_LINE_WIDTH_RANGE) ++bad_calls;
    else if (pname==GL_ALIASED_LINE_WIDTH_RANGE) ++valid_calls;
    vals[0]=1.0f; vals[1]=4.0f;
}
'''
SUFFIX = r'''
int main() {
    applyOffset(0, true);
    applyOffset(3, true);
    applyOffset(3, false);
    applyWidth();
    FakeLazy lazy;
    lazy.init();
    lazy.sendTransparency(1);
    lazy.sendTransparency(0);
    assert(lazy.glstate.stipplenum==0);
    assert(lazy.cachebitmask & TRANSPARENCY_MASK);
    assert(lazy.diffuse==1);
    assert(!lazy.colorindex);
#if defined(__ANDROID__) || defined(ANDROID)
    assert(bad_calls==0);
#else
    assert(bad_calls>0);
#endif
}
'''

class AndroidCoinGlesTest(unittest.TestCase):
    def stage(self, root):
        glue = root / 'src/glue/gl.cpp'
        lazy = root / 'src/elements/GL/SoGLLazyElement.cpp'
        line = root / 'src/elements/GL/SoGLLineWidthElement.cpp'
        glue.parent.mkdir(parents=True)
        lazy.parent.mkdir(parents=True)
        glue.write_text(GLUE)
        lazy.write_text(LAZY)
        line.write_text(LINEWIDTH)
        return glue, lazy, line

    def test_native_and_desktop_compile_and_behavior(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            glue, lazy, line = self.stage(root)
            patcher.patch(root)
            code = PREFIX + glue.read_text() + lazy.read_text() + line.read_text() + SUFFIX
            source = root/'test.cpp'
            source.write_text(code)
            for definition in (None, '__ANDROID__', 'ANDROID'):
                exe = root / (definition or 'desktop')
                cmd = ['g++', '-std=c++17', '-Wall', '-Wextra',
                       str(source), '-o', str(exe)]
                if definition:
                    cmd.insert(1, '-D' + definition)
                subprocess.run(cmd, check=True, capture_output=True)
                subprocess.run([str(exe)], check=True)

    def test_source_drift_fails_without_partial_write(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            glue, lazy, line = self.stage(root)
            lazy.write_text('unexpected pinned Coin revision')
            with self.assertRaises(RuntimeError):
                patcher.patch(root)
            self.assertEqual(glue.read_text(), GLUE)
            self.assertEqual(line.read_text(), LINEWIDTH)

if __name__ == '__main__':
    unittest.main()
