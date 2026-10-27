#!/usr/bin/env python3
"""Guard four GLES-invalid desktop OpenGL enums in the pinned FreeCAD Coin fork.

Called after the FreeCAD submodule checkout and before native CMake configure.
Only Android's GLES compilation is changed. Desktop behavior is preserved.
This does not emulate screen-door stippling or line/point polygon offset.
"""
from pathlib import Path
import sys


def replace_once(source: str, old: str, new: str, location: str) -> str:
    if source.count(old) != 1:
        raise RuntimeError(f"Pinned Coin source drift in {location}: expected 1 anchor, found {source.count(old)}")
    return source.replace(old, new, 1)


def patch(coin: Path) -> None:
    glue_path = coin / "src/glue/gl.cpp"
    lazy_path = coin / "src/elements/GL/SoGLLazyElement.cpp"
    glue = glue_path.read_text()
    lazy = lazy_path.read_text()

    # GLES only allows GL_POLYGON_OFFSET_FILL. The original Coin code also
    # toggles desktop-only LINE/POINT (both invalid on Adreno GLES 3.2).
    offset_enable = """      if (m & cc_glglue_LINES) glEnable(GL_POLYGON_OFFSET_LINE);
      else glDisable(GL_POLYGON_OFFSET_LINE);
      if (m & cc_glglue_POINTS) glEnable(GL_POLYGON_OFFSET_POINT);
      else glDisable(GL_POLYGON_OFFSET_POINT);"""
    glue = replace_once(glue, offset_enable,
                        "#ifndef __ANDROID__\n" + offset_enable + "\n#endif",
                        str(glue_path))
    offset_disable = """      if (m & cc_glglue_LINES) glDisable(GL_POLYGON_OFFSET_LINE);
      if (m & cc_glglue_POINTS) glDisable(GL_POLYGON_OFFSET_POINT);"""
    glue = replace_once(glue, offset_disable,
                        "#ifndef __ANDROID__\n" + offset_disable + "\n#endif",
                        str(glue_path))

    # OpenGL ES has no fixed-function GL_POLYGON_STIPPLE. Keep Coin's tracked
    # transparency state, but do not make invalid GL calls. A full screen-door
    # transparency emulation would require a separate tested shader change.
    stipple = """  if (stipplenum == 0) {
    glDisable(GL_POLYGON_STIPPLE);
  }
  else {
    if (this->glstate.stipplenum <= 0) glEnable(GL_POLYGON_STIPPLE);
    glPolygonStipple(stipple_patterns[stipplenum]);
  }
  this->glstate.stipplenum = stipplenum;"""
    lazy = replace_once(lazy, stipple,
                        "#ifndef __ANDROID__\n" + stipple[:-len("  this->glstate.stipplenum = stipplenum;")] +
                        "#endif\n  this->glstate.stipplenum = stipplenum;",
                        str(lazy_path))
    # GLES always has RGBA color buffers; GL_RGBA_MODE (0x0C31) is a desktop
    # GL query and raises GL_INVALID_ENUM on this device. Keep the original
    # desktop code and diffuse initialization exactly as before.
    init = """  glDisable(GL_POLYGON_STIPPLE);

  GLboolean rgba;
  glGetBooleanv(GL_RGBA_MODE, &rgba);
  if (!rgba) this->colorindex = TRUE;
  else {
    this->sendPackedDiffuse(0xccccccff);
  }"""
    gles = """#ifdef __ANDROID__
  this->colorindex = FALSE;
  this->sendPackedDiffuse(0xccccccff);
#else
""" + init + "\n#endif"
    lazy = replace_once(lazy, init, gles, str(lazy_path))

    # No partial writes if either pinned source changed unexpectedly.
    glue_path.write_text(glue)
    lazy_path.write_text(lazy)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch-android-coin-gles.py <FreeCAD/src/3rdParty/coin>")
    patch(Path(sys.argv[1]))
    print("Android-only Coin GLES enum guards installed (polygon offset, stipple, RGBA mode)")
