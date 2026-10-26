#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Real surfaceless GLES pixel regression. Build pinned GL4ES with NOEGL=ON,
# NOX11=ON, NO_INIT_CONSTRUCTOR=ON. Run once unpatched, then --assert-fixed.
import ctypes as C, os, sys
os.environ['EGL_PLATFORM']='surfaceless'
os.environ['LIBGL_ES']='2'
os.environ['LIBGL_NOBANNER']='1'
E=C.CDLL('libEGL.so.1')
P=C.c_void_p; I=C.c_int; U=C.c_uint; F=C.c_float

def fn(lib,name,ret,*args):
 f=getattr(lib,name); f.restype=ret; f.argtypes=args; return f
getdisplay=fn(E,'eglGetDisplay',P,P)
d=getdisplay(None)
a=I();b=I()
assert fn(E,'eglInitialize',U,P,P,P)(d,C.byref(a),C.byref(b)), 'EGL init'
attrs=(I*13)(0x3033,1,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038)
cfg=P(); n=I()
assert fn(E,'eglChooseConfig',U,P,P,P,I,P)(d,attrs,C.byref(cfg),1,C.byref(n)) and n.value
surface=fn(E,'eglCreatePbufferSurface',P,P,P,P)(d,cfg,(I*5)(0x3057,64,0x3056,64,0x3038))
ctx=fn(E,'eglCreateContext',P,P,P,P,P)(d,cfg,None,(I*3)(0x3098,2,0x3038))
assert fn(E,'eglMakeCurrent',U,P,P,P,P)(d,surface,surface,ctx)
getproc=fn(E,'eglGetProcAddress',P,C.c_char_p)
def native(name,ret,*args):return C.CFUNCTYPE(ret,*args)(getproc(name.encode()))
print('Native',native('glGetString',C.c_char_p,U)(0x1f02).decode())
os.environ.setdefault('LIBGL_GLES', 'libEGL.so.1')
L=C.CDLL(sys.argv[1],mode=os.RTLD_LOCAL|os.RTLD_NOW)
callback=C.CFUNCTYPE(P,C.c_char_p)(lambda name:getproc(name))
fn(L,'set_getprocaddress',None,P)(callback)
fn(L,'initialize_gl4es',None)()
vp=fn(L,'glViewport',None,I,I,I,I); vp(0,0,64,64)
fn(L,'glMatrixMode',None,U)(0x1701)
fn(L,'glLoadIdentity',None)()
fn(L,'glOrtho',None,C.c_double,C.c_double,C.c_double,C.c_double,C.c_double,C.c_double)(0,64,0,64,-1,1)
fn(L,'glMatrixMode',None,U)(0x1700); fn(L,'glLoadIdentity',None)()
fn(L,'glDisable',None,U)(0x0b71)
fn(L,'glBlendFunc',None,U,U)(0x0302,0x0303)
# Initialize the ordinary CAD shader before SoImage, as the real scene does.
fn(L,'glBegin',None,U)(4)
for x,y in ((0,0),(1,0),(0,1)):
 fn(L,'glVertex3f',None,F,F,F)(x,y,0)
fn(L,'glEnd',None)(); fn(L,'glFinish',None)()
native('glGetError',U)()
pix=(C.c_ubyte*(8*8*4))()
for y in range(8):
 for x in range(8):
  if y in (2,3,4):
   k=4*(y*8+x); pix[k:k+4]=[255,0,0,128 if y==2 else 255]
read=native('glReadPixels',None,I,I,I,I,U,U,P)
get=native('glGetIntegerv',None,U,P)
isenabled=fn(L,'glIsEnabled',C.c_ubyte,U)
results=[]
partial_results=[]
for enabled in (True,False,True):
 fn(L,'glEnable' if enabled else 'glDisable',None,U)(0x0be2)
 fn(L,'glClearColor',None,F,F,F,F)(0.2,0.4,0.6,1)
 fn(L,'glClear',None,U)(0x4000)
 fn(L,'glFinish',None)()
 before=(C.c_ubyte*4)(); read(11,11,1,1,0x1908,0x1401,before); print('before',list(before),'err',hex(native('glGetError',U)()))
 fn(L,'glRasterPos3f',None,F,F,F)(10,10,0)
 fn(L,'glDrawPixels',None,I,I,U,U,P)(8,8,0x1908,0x1401,pix)
 fn(L,'glFinish',None)()
 bg=(C.c_ubyte*4)(); red=(C.c_ubyte*4)()
 read(11,11,1,1,0x1908,0x1401,bg); read(11,13,1,1,0x1908,0x1401,red)
 partial=(C.c_ubyte*4)(); read(11,12,1,1,0x1908,0x1401,partial); partial_results.append(list(partial))
 error=native('glGetError',U)()
 logical=bool(isenabled(0x0be2))
 print('blend',enabled,'background',list(bg),'glyph',list(red),'logicalAfter',logical,'error',hex(error))
 assert logical==enabled
 results.append((list(bg),list(red),error))
if '--assert-fixed' in sys.argv:
 assert all(abs(a-b)<=1 for a,b in zip(partial_results[0][:3],[153,51,76])),partial_results
 assert partial_results[1][:3]==[255,0,0],partial_results
 assert results[0][0][:3]==[51,102,153],results
 assert results[1][0][:3]==[0,0,0],results
 assert results[2][0][:3]==[51,102,153],results
 assert all(r[1][:3]==[255,0,0] for r in results),results
 print('PASS: transparent and partial-alpha pixels, opaque glyph, blend-off semantics, repeat and logical state')
