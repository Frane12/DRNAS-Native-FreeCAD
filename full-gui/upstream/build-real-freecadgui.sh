#!/usr/bin/env bash
# Compile the ACTUAL pinned upstream src/Gui/FreeCADGui Android ARM64 target.
# After success, downstream GitHub Actions packages the real FreeCADGui and
# its bundled Coin3D/GL4ES/OCCT runtime into a separate QtActivity ARM64 APK.
# Keep original successful headless FreeCADBase/App build script unchanged.
# Failure is an honest GUI dependency blocker; do not quietly substitute
# our working OCCT Android viewport for upstream FreeCADGui.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/upstream-freecad-gui"
mkdir -p "$OUT"
# Align the legacy GL4ES extension typedef with Android GLES2/Qt headers.
# This const qualifier changes no binary ABI; keep the real GL implementation.
python3 - "$OUT/opengl-compat/include/GL/glext.h" <<'PY_GL_HEADER'
from pathlib import Path
import sys
header = Path(sys.argv[1])
source = header.read_text()
declarations = [
    (
        'typedef void (APIENTRYP PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar* *string, const GLint *length);',
        'typedef void (APIENTRYP PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);',
    ),
    (
        'GLAPI void APIENTRY glShaderSource (GLuint shader, GLsizei count, const GLchar* *string, const GLint *length);',
        'GLAPI void APIENTRY glShaderSource (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);',
    ),
]
for old, new in declarations:
    if old in source:
        assert source.count(old) == 1, 'Ambiguous GL4ES shader-source declaration'
        source = source.replace(old, new)
    else:
        assert new in source, 'Pinned GL4ES shader-source declaration changed unexpectedly'
header.write_text(source)
print('GL4ES shader-source typedef and prototype match Android GLES2 const qualifiers')
PY_GL_HEADER
base="$ROOT/full-port/build-full-core.sh"
driver="$OUT/genuine-FreeCADGui-build-driver.sh"
python3 - "$base" "$driver" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
# Driver lives under out/, so pin ROOT to the actual repository root.
root = str(Path(sys.argv[1]).resolve().parent.parent)
source_root = 'ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"'
assert s.count(source_root) == 1
s = s.replace(source_root, 'ROOT="' + root + '"')
assert s.count('-DBUILD_GUI=OFF \\') == 1, "Core script's GUI switch changed unexpectedly"
s=s.replace('-DBUILD_GUI=OFF \\', '-DBUILD_GUI=ON \\')
# Keep first configure as complete FreeCAD with real GUI and intended CAD modules.
# No stubs for missing COIN/OpenGL/GLU/PySide or other prerequisites.
s=s.replace('GUI=OFF (first full native core target FreeCADBase+FreeCADApp)',
            'GUI=ON (actual upstream FreeCADGui target; do not fake Coin3D)')
assert s.count('--target FreeCADBase FreeCADApp') == 1
s=s.replace('--target FreeCADBase FreeCADApp',
            '--target FreeCADBase FreeCADApp FreeCADGui')
# Attempt genuine native FreeCADGui and fail if its .so is absent.
needle = "echo 'FULL FreeCADBase + FreeCADApp Android ARM64: BUILD PASS' | tee \"$OUT/full-core-result.txt\""
assert s.count(needle)==1
s=s.replace(needle, """
gui="$(find "$BUILDDIR" -type f -name 'libFreeCADGui.so' -print -quit)"
test -n "$gui" || { echo 'REAL upstream FreeCADGui library missing' >&2; exit 1; }
readelf -h "$gui" | grep -q 'Machine:.*AArch64'
readelf -h "$gui" | grep -q 'Type:.*DYN'
! readelf -d "$gui" | grep -Eq 'libc\\\\.so\\\\.6|ld-linux'
sha256sum "$gui" | tee -a "$OUT/full-gui-sha256.txt"
echo 'UPSTREAM FreeCADGui Android ARM64 compiled: BUILD PASS' | tee "$OUT/full-gui-result.txt"
""")
# Preserve and tag the GUI CMake/configure/build logs under own output dir.
s=s.replace('OUT="$ROOT/out/full-freecad"', 'OUT="$ROOT/out/upstream-freecad-gui"')
s=s.replace('BUILDDIR="$ROOT/.build/freecad-upstream-android-arm64"',
            'BUILDDIR="$ROOT/.build/freecad-upstream-real-gui-arm64"')
# Use actual Android AArch64 GL4ES + GLU, not host Linux libGL and not
# fake GLU stubs; the real GL4ES NDK build is pinned and validated separately.
qt_flag = '  -DQT_HOST_PATH="$QT_HOST" ' + chr(92)
assert s.count(qt_flag) == 1
glflags = '''  -DOPENGL_INCLUDE_DIR="$ROOT/out/upstream-freecad-gui/opengl-compat/include" \\
  -DOPENGL_gl_LIBRARY="$ROOT/out/upstream-freecad-gui/opengl-compat/lib/libGL.so" \\
  -DOPENGL_glu_LIBRARY="$ROOT/out/upstream-freecad-gui/opengl-compat/lib/libGLU.so" \\
  -DOPENGL_opengl_LIBRARY="$ROOT/out/upstream-freecad-gui/opengl-compat/lib/libGL.so" \\
  -DOPENGL_GLU_FOUND=TRUE \\
  -DCOIN_BUILD_GLX=OFF \\
  -DCOIN_BUILD_EGL=OFF \\
  -DFREECAD_RCC_COMPRESSION_ALGO=zlib \\
  -DFREECAD_RCC_COMPRESSION_LEVEL=9 \\
'''
s = s.replace(qt_flag, glflags + qt_flag)
# FreeCAD's legacy custom GLU detection does not honor the explicitly
# imported Android GLU file under recent cross-compiling FindOpenGL.
# Only report GLU present if ALL real Android libraries/headers exist.
config_marker="echo 'Full upstream FreeCAD configure for Android arm64-v8a — no host ABI fallbacks'"
assert s.count(config_marker) == 1
validate_glu = """python3 - "$SOURCE/cMake/FreeCAD_Helpers/SetupOpenGL.cmake" <<'PY_VALIDATE_GLU'
from pathlib import Path
import sys
path=Path(sys.argv[1])
source=path.read_text()
needle='    if(OPENGL_GLU_FOUND)'
assert source.count(needle)==1
check='''    if(ANDROID)
        if(EXISTS "${OPENGL_gl_LIBRARY}" AND EXISTS "${OPENGL_glu_LIBRARY}"
            AND EXISTS "${OPENGL_INCLUDE_DIR}/GL/gl.h"
            AND EXISTS "${OPENGL_INCLUDE_DIR}/GL/glu.h")
            set(OPENGL_GLU_FOUND TRUE)
            message(STATUS "Verified real Android AArch64 GL4ES and Mesa GLU libraries")
        else()
            message(FATAL_ERROR "Required REAL Android GL4ES/GLU paths or headers are missing")
        endif()
    endif()
'''
path.write_text(source.replace(needle,check+needle))
PY_VALIDATE_GLU
"""
# Bundled Coin3D compiles native EGL offscreen glue under Android, so
# libCoin.so must link against the *real NDK libEGL* (no fake symbols).
# Set the link dependency after add_subdirectory(src) defines Coin.
coin_egl_patch = """python3 - "$SOURCE/CMakeLists.txt" <<'PY_COIN_EGL'
from pathlib import Path
import sys
path = Path(sys.argv[1])
cmake = path.read_text()
anchor = 'add_subdirectory(src)' + chr(10)
assert cmake.count(anchor) == 1, 'Upstream FreeCAD src anchor changed'
addition = '''
if(ANDROID)
    if(NOT TARGET Coin)
        message(FATAL_ERROR "Real bundled Coin3D target is missing")
    endif()
    target_link_libraries(Coin PRIVATE EGL)
    # FreeCADGui includes legacy GL/gl.h directly; the NDK itself ships
    # GLES headers, so expose the verified real Android GL4ES headers.
    if(NOT TARGET FreeCADGui)
        message(FATAL_ERROR "Real upstream FreeCADGui target is missing")
    endif()
    target_include_directories(FreeCADGui PRIVATE "${OPENGL_INCLUDE_DIR}")
    message(STATUS "Real Android NDK EGL linked to libCoin.so and GL4ES headers to FreeCADGui")
endif()
'''
path.write_text(cmake.replace(anchor, anchor + addition))
PY_COIN_EGL
"""
# libc++ eagerly checks diagnostic format arguments in these templates.
# Pass the same text used by Value's formatter explicitly.
style_format_patch = """python3 - "$SOURCE/src/Gui/StyleParameters" <<'PY_STYLE_FORMAT'
from pathlib import Path
import sys
root = Path(sys.argv[1])
replacements = {
    'Value.h': (
        'name, valueTypeName<T>(), *found);',
        'name, valueTypeName<T>(), found->toString());',
    ),
    'Gradient.h': (
        'tupleKindName(Kind), Value {source});',
        'tupleKindName(Kind), Value {source}.toString());',
    ),
}
for filename, (old, new) in replacements.items():
    path = root / filename
    source = path.read_text()
    assert source.count(old) == 1, f'Unexpected upstream diagnostic in {filename}'
    path.write_text(source.replace(old, new))
print('Style diagnostics use explicit Value string representation for libc++')
PY_STYLE_FORMAT
"""
# OpenGL ES has multisampling defined by framebuffer/sample count, not a
# desktop GL_MULTISAMPLE enable/disable capability. Preserve desktop code,
# but omit unsupported GL_MULTISAMPLE state toggles in Android Quarter UI.
quarter_android_patch = r"""python3 - "$SOURCE/src/Gui/Quarter/QuarterWidget.cpp" <<'PY_QUARTER_ANDROID'
from pathlib import Path
import sys
path = Path(sys.argv[1])
source = path.read_text()
state = '    QOpenGLFunctions* functions = w->context() ? w->context()->functions() : nullptr;\n    const bool multisampleEnabled = functions && functions->glIsEnabled(GL_MULTISAMPLE) == GL_TRUE;'
restore = '''    if (functions) {
        if (multisampleEnabled) {
            functions->glEnable(GL_MULTISAMPLE);
        }
        else {
            functions->glDisable(GL_MULTISAMPLE);
        }
    }'''
assert source.count(state) == 1, 'Upstream Quarter multisample state changed'
assert source.count(restore) == 1, 'Upstream Quarter multisample restore changed'
source = source.replace(state, '#ifndef ANDROID\n' + state + '\n#endif')
source = source.replace(restore, '#ifndef ANDROID\n' + restore + '\n#endif')
path.write_text(source)
print('Quarter Android GLES2 omits unsupported GL_MULTISAMPLE toggling')
PY_QUARTER_ANDROID
"""
# FreeCAD's upstream Python 3.11+ embedding uses an isolated PyConfig and
# therefore ignores PYTHONHOME/PYTHONPATH.  The original GUI reached
# Py_InitializeFromConfig on device, but failed there because the APK carried
# libpython3.14.so with *no* encodings/stdlib. Keep the original interpreter,
# explicitly pointing it at our verified Android CPython 3.14 assets. Also
# retain the real PyStatus explanation instead of losing it in a generic -3.
android_python_patch = """python3 - "$SOURCE/src/Base/Interpreter.cpp" <<'PY_ANDROID_EMBED_PYTHON'
from pathlib import Path
import sys
path = Path(sys.argv[1])
source = path.read_text()
needle = '''    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        throw Base::RuntimeError("Failed to init from config");
    }'''
assert source.count(needle) == 1, "Pinned upstream Python init block changed"
replacement = '''#ifdef __ANDROID__
    // QtActivity stages the authentic CPython 3.14 Android stdlib under its
    // app-private files directory. PyConfig_InitIsolatedConfig explicitly
    // ignores PYTHONPATH, so supply the actual module paths through PyConfig.
    const char* drnasPython = getenv("DRNAS_PYTHON_RUNTIME");
    const char* drnasNative = getenv("DRNAS_PYTHON_NATIVE_DIR");
    if (drnasPython && *drnasPython) {
        status = PyConfig_SetBytesString(&config, &config.home, drnasPython);
        if (PyStatus_Exception(status)) {
            throw Base::RuntimeError(std::string("Python home: ")
                + (status.err_msg ? status.err_msg : "configuration error"));
        }
        const std::string root(drnasPython);
        std::vector<std::string> paths = {
            root + "/python314.zip", root + "/lib-dynload"
        };
        if (drnasNative && *drnasNative) {
            paths.emplace_back(drnasNative);
        }
        for (const std::string& modulePath : paths) {
            wchar_t* wide = Py_DecodeLocale(modulePath.c_str(), nullptr);
            if (!wide) {
                throw Base::RuntimeError("Could not decode Android Python path");
            }
            status = PyWideStringList_Append(&config.module_search_paths, wide);
            PyMem_RawFree(wide);
            if (PyStatus_Exception(status)) {
                throw Base::RuntimeError(std::string("Python module path: ")
                    + (status.err_msg ? status.err_msg : "configuration error"));
            }
        }
        config.module_search_paths_set = 1;
    }
#endif
    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        throw Base::RuntimeError(std::string("Failed to init from config: ")
            + (status.err_msg ? status.err_msg : "unknown Python error"));
    }'''
# App::Interpreter.cpp already depends on std::vector from FreeCAD headers;
# include it explicitly so the NDK build is independent of include order.
source = source.replace(needle, replacement)
include = '#include <sstream>'
assert source.count(include) == 1
source = source.replace(include, include + '\\n#include <vector>')
path.write_text(source)
print('Android FreeCAD isolated PyConfig now resolves official CPython 3.14 stdlib')
PY_ANDROID_EMBED_PYTHON
"""
# The Android entrypoint lives inside the genuine libFreeCADGui.so, avoiding
# any eager DT_NEEDED dependency from QtLoader's main QtActivity library.
# This keeps the already-working OCCT viewer in safe mode until explicitly
# opted in on the phone. Include a real C++ bridge (not UI imitation).
android_bridge_patch = """python3 - "$SOURCE/src/Gui/CMakeLists.txt" "$ROOT/full-gui/upstream/AndroidMainWindowBridge.cpp" "$SOURCE/src/Gui/AndroidMainWindowBridge.cpp" <<'PY_ANDROID_BRIDGE'
from pathlib import Path
from shutil import copyfile
import sys
cmake_path, bridge_source, bridge_dest = map(Path, sys.argv[1:])
text = cmake_path.read_text()
anchor = 'add_library(FreeCADGui SHARED)'
assert text.count(anchor) == 1, 'Pinned upstream FreeCADGui target changed'
addition = '\\ntarget_sources(FreeCADGui PRIVATE AndroidMainWindowBridge.cpp)\\n'
cmake_path.write_text(text.replace(anchor, anchor + addition, 1))
copyfile(bridge_source, bridge_dest)
print('Real upstream FreeCADGui Android MainWindow bridge compiled into FreeCADGui')
PY_ANDROID_BRIDGE
"""
s=s.replace(config_marker,validate_glu+coin_egl_patch+style_format_patch+quarter_android_patch+android_python_patch+android_bridge_patch+config_marker)
Path(sys.argv[2]).write_text(s)
PY
# Uses pinned FreeCAD source, verified Android Qt/OCCT/ICU/Boost/Python/Xerces
# and native NDK toolchain (same exact versions as proven core build).
bash -n "$driver"
bash "$driver"
