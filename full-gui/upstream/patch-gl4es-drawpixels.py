#!/usr/bin/env python3
"""Keep desktop glDrawPixels blending in its own GLES2 blit mode.

Pinned GL4ES a025615. Ordinary framebuffer/texture blits keep their upstream
behavior. Diagnostics are bounded; no pixel reads or file I/O in the draw loop.
"""
from pathlib import Path
import sys


def patch(root):
    changes = {}

    def replace(file, old, new):
        path = root / file
        source = changes.get(path, path.read_text())
        if source.count(old) != 1:
            raise RuntimeError(f"Pinned GL4ES anchor changed in {file}: {old[:100]!r}")
        changes[path] = source.replace(old, new, 1)

    replace("blit.h", "#define BLIT_COLOR      2",
            "#define BLIT_COLOR      2\n#define BLIT_DRAWPIXELS 3")
    replace("raster.c", "(rast->bitmap)?BLIT_ALPHA:BLIT_COLOR",
            "(rast->bitmap)?BLIT_ALPHA:BLIT_DRAWPIXELS")
    replace("raster.c", "\tr->width = glstate->raster.raster_width;",
            """    // TexParameter calls above update GL4ES's deferred sampler only.
    // This raster texture bypasses normal texture drawing, so materialize its
    // nearest filter before returning to the old binding. Without this, GLES
    // retains its default mipmap filter for a level-0-only texture and samples
    // opaque black, even when the uploaded RGBA texels are correct.
    realize_1texture(GL_TEXTURE_2D, 0,
                     gl4es_getTexture(GL_TEXTURE_2D, r->texture), NULL);
\tr->width = glstate->raster.raster_width;""")
    replace("blit.c", "    gl4es_glDisable(GL_BLEND);\n    int alpha = 0;",
            """    // DrawPixels is a fragment operation: retain the caller's blending.
    // Other users of this shared blitter still require an opaque copy.
    if (mode != BLIT_DRAWPIXELS) gl4es_glDisable(GL_BLEND);
    int alpha = 0;""")
    replace("blit.c", "    realize_blitenv(alpha);\n\n    gles_glDrawArrays(GL_TRIANGLE_FAN, 0, 4);",
            """    realize_blitenv(alpha);

    // Native state can differ from GL4ES's cache after Qt rendering, and
    // shader-emulated blending is not part of this dedicated blit shader.
    // Apply tracked factors directly for DrawPixels and restore native state.
    GLboolean drnas_old_blend = GL_FALSE;
    GLint drnas_old_factors[4] = {0, 0, 0, 0};
    GLint drnas_old_equations[2] = {0, 0};
    if (mode == BLIT_DRAWPIXELS) {
        LOAD_GLES(glIsEnabled);
        LOAD_GLES(glGetIntegerv);
        LOAD_GLES(glEnable);
        LOAD_GLES(glDisable);
        LOAD_GLES2(glBlendFuncSeparate);
        LOAD_GLES2(glBlendEquationSeparate);
        drnas_old_blend = gles_glIsEnabled(GL_BLEND);
        gles_glGetIntegerv(GL_BLEND_SRC_RGB, &drnas_old_factors[0]);
        gles_glGetIntegerv(GL_BLEND_DST_RGB, &drnas_old_factors[1]);
        gles_glGetIntegerv(GL_BLEND_SRC_ALPHA, &drnas_old_factors[2]);
        gles_glGetIntegerv(GL_BLEND_DST_ALPHA, &drnas_old_factors[3]);
        gles_glGetIntegerv(GL_BLEND_EQUATION_RGB_OES, &drnas_old_equations[0]);
        gles_glGetIntegerv(GL_BLEND_EQUATION_ALPHA_OES, &drnas_old_equations[1]);
        if (glstate->enable.blend) gles_glEnable(GL_BLEND);
        else gles_glDisable(GL_BLEND);
        gles_glBlendFuncSeparate(glstate->blendsfactorrgb, glstate->blenddfactorrgb,
                                glstate->blendsfactoralpha, glstate->blenddfactoralpha);
        gles_glBlendEquationSeparate(glstate->blendeqrgb, glstate->blendeqalpha);
        static unsigned drnas_blit_reports = 0;
        if (drnas_blit_reports++ < 16) {
            GLint bound = 0, program = 0;
            gles_glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
            gles_glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            LOGD("DRAWPIXELS_BLIT texture=%u bound=%d program=%d blend=%d nativeBefore=%d src=0x%x dst=0x%x size=%.0fx%.0f\\n",
                 texture, bound, program, glstate->enable.blend, drnas_old_blend,
                 glstate->blendsfactorrgb, glstate->blenddfactorrgb, width, height);
        }
    }

    gles_glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    if (mode == BLIT_DRAWPIXELS) {
        LOAD_GLES(glEnable);
        LOAD_GLES(glDisable);
        LOAD_GLES2(glBlendFuncSeparate);
        LOAD_GLES2(glBlendEquationSeparate);
        if (drnas_old_blend) gles_glEnable(GL_BLEND);
        else gles_glDisable(GL_BLEND);
        gles_glBlendFuncSeparate(drnas_old_factors[0], drnas_old_factors[1],
                                drnas_old_factors[2], drnas_old_factors[3]);
        gles_glBlendEquationSeparate(drnas_old_equations[0], drnas_old_equations[1]);
    }""")
    replace("raster.c", '#include "pixel.h"', '#include "pixel.h"\n#include "logs.h"')
    replace("raster.c", "\traster_to_texture(r);\n\tr->xmove = 0;",
            """    // Inspect the converted pixels actually uploaded, after pixel transfer.
    static unsigned drnas_rgba_reports = 0;
    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE && width > 0 && height > 0
        && width <= 256 && height <= 256 && drnas_rgba_reports++ < 16) {
        unsigned zero = 0, partial = 0, opaque = 0, colored = 0;
        const GLubyte* p = glstate->raster.data;
        for (int i = 0; i < width*height; ++i, p += 4) {
            if (!p[3]) ++zero;
            else if (p[3] == 255) ++opaque;
            else ++partial;
            if (p[3] && (p[0] || p[1] || p[2])) ++colored;
        }
        LOGD("DRAWPIXELS_RGBA size=%dx%d zero=%u partial=%u opaque=%u colored=%u blend=%d pos=%.1f,%.1f\\n",
             width, height, zero, partial, opaque, colored, glstate->enable.blend,
             glstate->raster.rPos.x, glstate->raster.rPos.y);
    }
\traster_to_texture(r);
\tr->xmove = 0;""")
    # A fresh marker is checked in the native build and APK provenance gate.
    path = root / "blit.c"
    changes[path] += '\n__attribute__((visibility("default"))) int drnas_gl4es_drawpixels_blend_v1(void) { return 1; }\n'
    for path, contents in changes.items():
        path.write_text(contents)


if __name__ == "__main__":
    patch(Path(sys.argv[1]))
    print("DRAWPIXELS_BLEND_V1: isolated raster mode; tracked native blending; bounded CPU/blit diagnostics")
