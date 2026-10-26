#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
from pathlib import Path
import sys
p = Path(sys.argv[1])
source = p.read_text()
old = """GLenum ReadDraw_Push(GLenum target) {"""
new = """#if defined(__ANDROID__) || defined(DRNAS_QT_BRIDGE_TEST)
#include <dlfcn.h>
__attribute__((visibility("default")))
int drnas_gl4es_qt_state_bridge_version(void) { return 3; }

// Unique marker required by both the runtime staging and final APK gate.
__attribute__((visibility("default")))
int drnas_gl4es_qt_raster_state_v3(void) { return 3; }

static void drnas_gl4es_restore_raster_state(void) {
    // Restore the CAD context's tracked state directly to native GLES.
    // Calling the GL4ES wrappers here would early-out on their cached values.
    // Do not replace Coin state with defaults: it may intentionally use a
    // clipped viewport, transparency, reversed faces or a read-only depth pass.
    LOAD_GLES(glEnable);
    LOAD_GLES(glDisable);
    LOAD_GLES(glScissor);
    LOAD_GLES(glViewport);
    LOAD_GLES(glColorMask);
    LOAD_GLES(glDepthMask);
    LOAD_GLES(glDepthFunc);
    LOAD_GLES(glDepthRangef);
    LOAD_GLES(glClearDepthf);
    LOAD_GLES(glClearColor);
    LOAD_GLES(glClearStencil);
    LOAD_GLES(glCullFace);
    LOAD_GLES(glFrontFace);
    LOAD_GLES2(glBlendColor);
    LOAD_GLES2(glBlendFuncSeparate);
    LOAD_GLES2(glBlendEquationSeparate);
    LOAD_GLES2(glStencilMaskSeparate);
    LOAD_GLES2(glStencilFuncSeparate);
    LOAD_GLES2(glStencilOpSeparate);
    #define DRNAS_RESTORE_ENABLE(cap, enabled) \
        do { if (enabled) gles_glEnable(cap); else gles_glDisable(cap); } while (0)
    DRNAS_RESTORE_ENABLE(GL_SCISSOR_TEST, glstate->enable.scissor_test);
    DRNAS_RESTORE_ENABLE(GL_DEPTH_TEST, glstate->enable.depth_test);
    DRNAS_RESTORE_ENABLE(GL_STENCIL_TEST, glstate->enable.stencil_test);
    DRNAS_RESTORE_ENABLE(GL_CULL_FACE, glstate->enable.cull_face);
    DRNAS_RESTORE_ENABLE(GL_BLEND, glstate->enable.blend && !globals4es.shaderblend);
    DRNAS_RESTORE_ENABLE(GL_POLYGON_OFFSET_FILL, glstate->enable.polyfill_offset);
    DRNAS_RESTORE_ENABLE(GL_SAMPLE_ALPHA_TO_COVERAGE, glstate->enable.sample_alpha_to_coverage);
    #undef DRNAS_RESTORE_ENABLE
    if (glstate->raster.scissor_known)
        gles_glScissor(glstate->raster.scissor.x, glstate->raster.scissor.y,
                      glstate->raster.scissor.width, glstate->raster.scissor.height);
    if (glstate->raster.viewport_known)
        gles_glViewport(glstate->raster.viewport.x, glstate->raster.viewport.y,
                       glstate->raster.viewport.width, glstate->raster.viewport.height);
    gles_glColorMask(glstate->colormask[0], glstate->colormask[1],
                     glstate->colormask[2], glstate->colormask[3]);
    gles_glDepthMask(glstate->depth.mask);
    gles_glDepthFunc(glstate->depth.func);
    gles_glDepthRangef(glstate->depth.Near, glstate->depth.Far);
    gles_glClearDepthf(glstate->depth.clear);
    gles_glClearColor(glstate->clear_color[0], glstate->clear_color[1],
                      glstate->clear_color[2], glstate->clear_color[3]);
    gles_glClearStencil(glstate->stencil.clear);
    gles_glCullFace(glstate->face.cull);
    gles_glFrontFace(glstate->face.front);
    gles_glBlendColor(glstate->blend_color[0], glstate->blend_color[1],
                      glstate->blend_color[2], glstate->blend_color[3]);
    gles_glBlendFuncSeparate(glstate->blendsfactorrgb, glstate->blenddfactorrgb,
                            glstate->blendsfactoralpha, glstate->blenddfactoralpha);
    gles_glBlendEquationSeparate(glstate->blendeqrgb, glstate->blendeqalpha);
    for (int i=0; i<2; ++i) {
        GLenum face=i ? GL_BACK : GL_FRONT;
        gles_glStencilMaskSeparate(face, glstate->stencil.mask[i]);
        gles_glStencilFuncSeparate(face, glstate->stencil.func[i],
                                  glstate->stencil.f_ref[i], glstate->stencil.f_mask[i]);
        gles_glStencilOpSeparate(face, glstate->stencil.sfail[i],
                                glstate->stencil.dpfail[i], glstate->stencil.dppass[i]);
    }
}

static void drnas_gl4es_resume_after_qt(void) {
    drnas_gl4es_restore_raster_state();
    // Qt writes native GLES state without updating GL4ES's hardware cache.
    // A cached program/attribute/buffer binding must never suppress the next
    // real GLES call after returning from QPainter/QOpenGLWidget composition.
    LOAD_GLES2(glUseProgram);
    LOAD_GLES(glBindBuffer);
    LOAD_GLES2(glDisableVertexAttribArray);
    static void* native_gles = NULL;
    typedef void (*BindVertexArrayProc)(unsigned int);
    static BindVertexArrayProc bind_vertex_array = NULL;
    if (!native_gles) {
        native_gles = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
        if (native_gles)
            bind_vertex_array = (BindVertexArrayProc)dlsym(native_gles, "glBindVertexArray");
    }
    // Client-side GL4ES arrays need GLES's default VAO, not Qt's private VAO.
    if (bind_vertex_array) bind_vertex_array(0);
    gles_glUseProgram(0);
    glstate->gleshard->program = (GLuint)-1;
    glstate->gleshard->glprogram = NULL;
    glstate->gleshard->active = (GLuint)-1;
    // Qt also binds its atlas/composition textures in the shared context.
    for (int i=0; i<hardext.maxtex; ++i) glstate->actual_tex2d[i] = (GLuint)-1;
    glstate->bound_changed = hardext.maxtex;
    gles_glBindBuffer(GL_ARRAY_BUFFER, 0);
    gles_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glstate->bind_buffer.array = 0;
    glstate->bind_buffer.index = 0;
    glstate->bind_buffer.want_index = 0;
    glstate->bind_buffer.used = 0;
    memset(glstate->gleshard->vertexattrib, 0, sizeof(glstate->gleshard->vertexattrib));
    memset(glstate->gleshard->vavalue, 0xff, sizeof(glstate->gleshard->vavalue));
    for (int i=0; i<hardext.maxvattrib; ++i) {
        gles_glDisableVertexAttribArray(i);
        // Neither boolean value: force realize_glenv to upload every used array.
        glstate->gleshard->vertexattrib[i].enabled = 2;
    }
}

__attribute__((visibility("default")))
int drnas_gl4es_bind_qt_default_fbo(unsigned int qtFbo) {
    // GL4ES has its own framebuffer namespace. Qt owns qtFbo and created
    // it directly through GLES; it must NOT be treated as a GL4ES-generated
    // nonzero FBO. Map GL4ES framebuffer zero to the real Qt GLES FBO.
    if (!glstate || !glstate->fbo.fbo_0 || !qtFbo) return -1;
    LOAD_GLES2_OR_OES(glIsFramebuffer);
    if (!gles_glIsFramebuffer(qtFbo)) return -2;
    if (!glstate->gleshard) return -4;
    drnas_gl4es_resume_after_qt();
    glstate->fbo.mainfbo_fbo = qtFbo;
    gl4es_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // GL4ES reports logical framebuffer 0, native GLES must report qtFbo.
    return 1;
}
#endif

GLenum ReadDraw_Push(GLenum target) {"""
if source.count(old) != 1:
    raise SystemExit("Pinned GL4ES ReadDraw_Push anchor changed")
p.write_text(source.replace(old, new, 1))
# Scissor enable was forwarded without tracking by pinned upstream. Capture
# the CAD state, including glPush/PopAttrib queries, instead of querying Qt's
# most recent native scissor enable at the next draw.
def replace_once(path, old, new):
    s = path.read_text()
    if s.count(old) != 1:
        raise SystemExit(f"Pinned GL4ES anchor changed in {path}: {old}")
    path.write_text(s.replace(old, new, 1))
root = p.parent
replace_once(root / "state.h", "              stencil_test,", "              stencil_test,\n              scissor_test,")
replace_once(root / "enable.c", "        proxy_GO(GL_STENCIL_TEST, stencil_test);", "        proxy_GO(GL_STENCIL_TEST, stencil_test);\n        proxy_GO(GL_SCISSOR_TEST, scissor_test);")
replace_once(root / "enable.c", "        isenabled(GL_STENCIL_TEST, stencil_test);", "        isenabled(GL_STENCIL_TEST, stencil_test);\n        isenabled(GL_SCISSOR_TEST, scissor_test);")
replace_once(root / "glstate.c", "    glstate->blendsfactorrgb = GL_ONE;", "    glstate->blendeqrgb = glstate->blendeqalpha = GL_FUNC_ADD;\n    glstate->blendsfactorrgb = GL_ONE;")
# Coin3D's Sketcher constraint SoImage emits RGBA glDrawPixels. In GL4ES
# raster.c that becomes a texture BLIT_COLOR (not BLIT_ALPHA). Pinned GLES2
# blit.c unconditionally disables GL_BLEND, so zero-alpha SVG background
# pixels become opaque black squares. Retain the caller's blending ONLY for
# BLIT_COLOR, leaving bitmap/opaque blits and caller-disabled blending intact.
# gl4es_glPushAttrib/glPopAttrib in blit.c owns restoration after drawing.
blit_path = root / "blit.c"
blit_source = blit_path.read_text()
blend_off = "    gl4es_glDisable(GL_BLEND);\\n    int alpha = 0;"
blend_save = (
    "    // DRNAS Sketcher SoImage glDrawPixels: preserve transparent RGBA glyphs.\\n"
    "    // GL4ES BLIT_COLOR must honor Coin's enabled blend state.\\n"
    "    const int drnas_cad_blend = glstate->enable.blend && !globals4es.shaderblend;\\n"
    "    gl4es_glDisable(GL_BLEND);\\n"
    "    int alpha = 0;"
)
color_case = "        case BLIT_COLOR:\\n            //gl4es_glColor4f(1.0f, 1.0f, 1.0f, 1.0f);\\n            break;"
color_restore = (
    "        case BLIT_COLOR:\\n"
    "            // glDrawPixels RGBA must composite transparent Sketcher overlays.\\n"
    "            if (drnas_cad_blend) gl4es_glEnable(GL_BLEND);\\n"
    "            break;"
)
assert blit_source.count(blend_off) == 1, "Pinned GLES2 BLIT blend guard changed"
assert blit_source.count(color_case) == 1, "Pinned GLES2 BLIT_COLOR case changed"
blit_source = blit_source.replace(blend_off, blend_save, 1)
blit_source = blit_source.replace(color_case, color_restore, 1)
blit_path.write_text(blit_source)
print("DRNAS Sketcher alpha: GLES2 BLIT_COLOR preserves caller GL_BLEND")
print("DRNAS Qt/GL4ES v3: native FBO ownership and tracked CAD raster state restored")
