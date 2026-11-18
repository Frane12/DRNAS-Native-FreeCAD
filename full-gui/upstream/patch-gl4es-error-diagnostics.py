#!/usr/bin/env python3
"""Add bounded, Android-only provenance for GL_INVALID_ENUM in pinned GL4ES.

This is an investigation build. It does not consume GLES errors or alter the
existing shim error-selection logic. An earlier shim report does not by itself
prove it is the error subsequently returned to Coin.
"""
from pathlib import Path
import sys


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"GL4ES source drift: expected exactly one {old!r}")
    return text.replace(old, new, 1)


def patch(root):
    header_path = root / 'gl4es.h'
    getter_path = root / 'getter.c'
    header = header_path.read_text()
    getter = getter_path.read_text()
    header = replace_once(header, 'static inline void errorShim(GLenum error) {',
                          'static inline void drnasOriginalErrorShim(GLenum error) {')
    marker = 'static inline void noerrorShim() {'
    header = replace_once(header, marker, '''#ifdef __ANDROID__
void drnas_gl4es_trace_shim_error(GLenum error, const char *file, int line, const char *function);
#define errorShim(error) do { \\
    GLenum drnas_error_value = (error); \\
    drnas_gl4es_trace_shim_error(drnas_error_value, __FILE__, __LINE__, __func__); \\
    drnasOriginalErrorShim(drnas_error_value); \\
} while (0)
#else
#define errorShim(error) drnasOriginalErrorShim(error)
#endif

''' + marker)
    getter = replace_once(getter, 'GLenum APIENTRY_GL4ES gl4es_glGetError(void) {', r'''#ifdef __ANDROID__
#include <stdio.h>
#include <stdlib.h>

/* Limit writes while retaining occasional samples in the rolling journal. */
static unsigned int drnas_shim_reports;
static unsigned int drnas_return_reports;
static void drnas_trace_error(const char *origin, const char *file, int line,
                              const char *function, unsigned int *counter) {
    unsigned int n = __atomic_add_fetch(counter, 1, __ATOMIC_RELAXED);
    if (n > 8 && n % 120 != 0) return;
    const char *path = getenv("DRNAS_WB_LOG_PATH");
    if (!path || !*path) return;
    FILE *out = fopen(path, "a");
    if (!out) return;
    fprintf(out, "[NEW_NATIVE] GL_ENUM_TRACE origin=%s file=%s line=%d function=%s sample=%u\n",
            origin, file, line, function, n);
    fclose(out);
}
void drnas_gl4es_trace_shim_error(GLenum error, const char *file, int line, const char *function) {
    if (error == GL_INVALID_ENUM && glstate && glstate->shim_error == GL_NO_ERROR)
        drnas_trace_error("shim-raised", file, line, function, &drnas_shim_reports);
}
#endif

GLenum APIENTRY_GL4ES gl4es_glGetError(void) {''')
    getter = replace_once(getter, '    GLenum err = GL_NO_ERROR;\n    if(!glstate->type_error) {',
        '''    GLenum err = GL_NO_ERROR;
#ifdef __ANDROID__
    const char *drnas_origin = "shim";
#endif
    if(!glstate->type_error) {''')
    getter = replace_once(getter, '        err = gles_glGetError();\n        // If no error, then check "shim" error',
        '''        err = gles_glGetError();
#ifdef __ANDROID__
        if (err != GL_NO_ERROR) drnas_origin = "native-gles";
#endif
        // If no error, then check "shim" error''')
    getter = replace_once(getter, '\n\treturn err;\n}\nAliasExport(GLenum,glGetError,,());',
        '''
#ifdef __ANDROID__
    if (err == GL_INVALID_ENUM)
        drnas_trace_error(drnas_origin, "getter.c", 0, "gl4es_glGetError", &drnas_return_reports);
#endif
\treturn err;
}
AliasExport(GLenum,glGetError,,());''')
    header_path.write_text(header)
    getter_path.write_text(getter)


if __name__ == '__main__':
    patch(Path(sys.argv[1]))
    print('GL4ES invalid-enum provenance diagnostics enabled')
