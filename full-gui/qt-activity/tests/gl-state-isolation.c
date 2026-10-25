// SPDX-License-Identifier: LGPL-2.1-or-later
// Run against the same patched GL4ES source as Android, using Mesa GLES/EGL.
// External native calls model Qt/QPainter bypassing GL4ES's state cache.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok, const char* message) {
    if (!ok) { fprintf(stderr,"FAIL: %s (GLES 0x%x, EGL 0x%x)\n",message,glGetError(),eglGetError()); exit(1); }
}
#define CAD(ret, name, args) ret (*cad_##name) args = (ret (*) args)dlsym(library, "gl" #name); check(cad_##name!=NULL, "missing GL4ES " #name)

int main(int argc, char** argv) {
    check(argc==2,"usage: gl-state-isolation /path/to/patched/libGL.so");
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    check(eglInitialize(display,NULL,NULL),"initialize EGL");
    const EGLint configAttrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig config; EGLint count;
    check(eglChooseConfig(display,configAttrs,&config,1,&count) && count==1,"choose GLES3 config");
    const EGLint surfaceAttrs[]={EGL_WIDTH,128,EGL_HEIGHT,96,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
    const EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
    check(eglMakeCurrent(display,surface,surface,context),"make native context current");
    void* library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if (!library) fprintf(stderr,"%s\n",dlerror());
    check(library!=NULL,"load real patched GL4ES");
    int (*bridge)(unsigned int)=(int (*)(unsigned int))dlsym(library,"drnas_gl4es_bind_qt_default_fbo");
    int (*version)(void)=(int (*)(void))dlsym(library,"drnas_gl4es_qt_raster_state_v3");
    check(bridge && version && version()==3,"require v3 raster bridge");
    CAD(void, Enable, (GLenum)); CAD(void, Disable, (GLenum));
    CAD(void, Viewport, (GLint,GLint,GLsizei,GLsizei));
    CAD(void, Scissor, (GLint,GLint,GLsizei,GLsizei));
    CAD(void, ColorMask, (GLboolean,GLboolean,GLboolean,GLboolean));
    CAD(void, DepthMask, (GLboolean)); CAD(void, DepthFunc, (GLenum));
    CAD(void, BlendFunc, (GLenum,GLenum)); CAD(void, CullFace, (GLenum));
    CAD(void, StencilFunc, (GLenum,GLint,GLuint));
    CAD(void, StencilMask, (GLuint));
    CAD(void, ClearColor, (GLfloat,GLfloat,GLfloat,GLfloat));
    CAD(void, Clear, (GLbitfield)); CAD(void, Begin, (GLenum));
    CAD(void, Color4f, (GLfloat,GLfloat,GLfloat,GLfloat));
    CAD(void, Vertex3f, (GLfloat,GLfloat,GLfloat));
    CAD(void, End, (void)); CAD(void, Finish, (void));
    CAD(GLboolean, IsEnabled, (GLenum));

    GLuint previousFbo=0, previousTexture=0;
    for (int size=0; size<2; ++size) {
        int width=size?128:64, height=size?96:64;
        GLuint texture, fbo;
        glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Qt-style native FBO complete");
        check(bridge(fbo)==1,"adopt new Qt-owned FBO");
        // The previous framebuffer stays alive until its replacement is bound,
        // matching QOpenGLWidget recreation without transferring ownership.
        if(previousFbo) glDeleteFramebuffers(1,&previousFbo);
        if(previousTexture) glDeleteTextures(1,&previousTexture);
        previousFbo=fbo; previousTexture=texture;
        cad_Viewport(0,0,width,height);
        cad_Scissor(3,4,width-6,height-8);
        cad_Enable(GL_SCISSOR_TEST); cad_Disable(GL_DEPTH_TEST);
        cad_Disable(GL_STENCIL_TEST); cad_Disable(GL_CULL_FACE);
        cad_Enable(GL_BLEND); cad_BlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        cad_DepthFunc(GL_LEQUAL); cad_DepthMask(GL_TRUE);
        cad_CullFace(GL_BACK); cad_StencilFunc(GL_EQUAL,3,0x3f); cad_StencilMask(0x5a);
        cad_ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        cad_ClearColor(0,0,0,1);
        unsigned char reference[4]={0};
        for (int pass=0; pass<3; ++pass) {
            if (pass) {
                glBindFramebuffer(GL_FRAMEBUFFER,0);
                glEnable(GL_SCISSOR_TEST); glScissor(0,0,0,0);
                glViewport(0,0,1,1); glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
                glEnable(GL_DEPTH_TEST); glDepthFunc(GL_NEVER); glDepthMask(GL_FALSE);
                glEnable(GL_STENCIL_TEST); glStencilFunc(GL_NEVER,0,0); glStencilMask(0);
                glEnable(GL_CULL_FACE); glCullFace(GL_FRONT_AND_BACK);
                glDisable(GL_BLEND); glBlendFunc(GL_ZERO,GL_ZERO);
                glUseProgram(0); glActiveTexture(GL_TEXTURE3);
                // None of these native changes may become CAD logical state.
                check(cad_IsEnabled(GL_SCISSOR_TEST),"CAD scissor enable survives external Qt state");
                check(bridge(fbo)==1,"resume CAD after native GUI draw");
            }
            GLint value=0, box[4], factors[4]; GLboolean mask[4];
            glGetIntegerv(GL_FRAMEBUFFER_BINDING,&value); check(value==(GLint)fbo,"native FBO restored");
            glGetIntegerv(GL_VIEWPORT,box); check(box[2]==width && box[3]==height,"physical viewport restored");
            glGetIntegerv(GL_SCISSOR_BOX,box); check(box[0]==3 && box[1]==4 && box[2]==width-6 && box[3]==height-8,"intentional CAD clipping preserved");
            glGetBooleanv(GL_COLOR_WRITEMASK,mask); check(mask[0]&&mask[1]&&mask[2]&&mask[3],"color writes restored");
            glGetIntegerv(GL_DEPTH_FUNC,&value); check(value==GL_LEQUAL,"nondefault CAD depth function restored");
            glGetBooleanv(GL_DEPTH_WRITEMASK,mask); check(mask[0],"depth writes restored");
            glGetIntegerv(GL_STENCIL_WRITEMASK,&value); check(value==0x5a,"CAD stencil mask preserved");
            glGetIntegerv(GL_BLEND_SRC_RGB,&factors[0]); glGetIntegerv(GL_BLEND_DST_RGB,&factors[1]);
            check(factors[0]==GL_SRC_ALPHA && factors[1]==GL_ONE_MINUS_SRC_ALPHA,"CAD blend factors preserved");
            check(glIsEnabled(GL_BLEND) && !glIsEnabled(GL_DEPTH_TEST) && !glIsEnabled(GL_STENCIL_TEST) && !glIsEnabled(GL_CULL_FACE),"CAD capabilities restored");
            cad_Clear(GL_COLOR_BUFFER_BIT);
            cad_Begin(GL_TRIANGLES); cad_Color4f(1,0.5f,0.25f,1);
            cad_Vertex3f(-0.8f,-0.8f,0); cad_Vertex3f(0.8f,-0.8f,0); cad_Vertex3f(0,0.8f,0);
            cad_End(); cad_Finish();
            unsigned char pixel[4]; glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            check(pixel[0]>240 && pixel[1]>110 && pixel[1]<145,"fixed-pipeline CAD triangle is visible");
            if (!pass) memcpy(reference,pixel,4);
            else check(!memcmp(reference,pixel,4),"pixels unchanged after Qt state contamination");
            check(glGetError()==GL_NO_ERROR,"no GLES errors after state handoff");
            printf("PASS: GUI/CAD GL isolation %dx%d pass %d pixel=%u,%u,%u,%u\n",width,height,pass,pixel[0],pixel[1],pixel[2],pixel[3]);
        }
    }
    return 0;
}
