package org.drnas.freecad.smoke;

import android.content.Context;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import android.view.MotionEvent;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

public final class CADRenderView extends GLSurfaceView implements GLSurfaceView.Renderer {
    private static final int STRIDE = 24;  // XYZ + normal, each float 4 bytes.
    private static final String VS =
        "attribute vec3 aPos;\n" +
        "attribute vec3 aNormal;\n" +
        "uniform mat4 uMVP;\n" +
        "uniform mat4 uModel;\n" +
        "varying vec3 vNormal;\n" +
        "void main() {\n" +
        " vNormal = normalize(mat3(uModel) * aNormal);\n" +
        " gl_Position = uMVP * vec4(aPos, 1.0);\n" +
        "}\n";
    private static final String FS =
        "precision mediump float;\n" +
        "varying vec3 vNormal;\n" +
        "uniform float uEdges;\n" +
        "void main() {\n" +
        " if (uEdges > 0.5) { gl_FragColor = vec4(0.78,0.91,1.0,1.0); return; }\n" +
        " vec3 n = normalize(vNormal);\n" +
        " vec3 l = normalize(vec3(0.6,0.8,1.0));\n" +
        " float diff = max(dot(n,l),0.0);\n" +
        " float fill = max(dot(n,-l),0.0)*0.18;\n" +
        " vec3 steel = vec3(0.25,0.57,0.88);\n" +
        " gl_FragColor=vec4(steel*(0.31+0.63*diff+fill)+vec3(0.035,0.075,0.12),1.0);\n" +
        "}\n";

    private final float[] projection = new float[16];
    private final float[] view = new float[16];
    private final float[] model = new float[16];
    private final float[] vp = new float[16];
    private final float[] mvp = new float[16];
    private FloatBuffer solid, edges;
    private int vertexCount, edgeCount, program;
    private float cx, cy, cz, scale = 1.0f;
    private float rotX=-22f, rotY=-30f, distance=4.4f, panX=0, panY=0;
    private float prevX, prevY, prevSpan;
    private int viewportWidth=1, viewportHeight=1;
    private boolean twoFinger, showEdges=false;

    public CADRenderView(Context context) {
        super(context);
        setEGLContextClientVersion(2);
        setPreserveEGLContextOnPause(true);
        setRenderer(this);
        setRenderMode(RENDERMODE_WHEN_DIRTY);
    }

    private static FloatBuffer direct(float[] values) {
        FloatBuffer b = ByteBuffer.allocateDirect(values.length * 4)
                .order(ByteOrder.nativeOrder()).asFloatBuffer();
        b.put(values); b.position(0);
        return b;
    }

    // Called only from GLSurfaceView.queueEvent().
    public void setTriangles(float[] mesh) {
        if (mesh==null || mesh.length<18 || mesh.length%18!=0) {
            solid=null; edges=null; vertexCount=0; edgeCount=0; requestRender();
            return;
        }
        float x0=Float.POSITIVE_INFINITY,y0=x0,z0=x0;
        float x1=Float.NEGATIVE_INFINITY,y1=x1,z1=x1;
        for (int i=0;i<mesh.length;i+=6) {
            x0=Math.min(x0,mesh[i]); y0=Math.min(y0,mesh[i+1]); z0=Math.min(z0,mesh[i+2]);
            x1=Math.max(x1,mesh[i]); y1=Math.max(y1,mesh[i+1]); z1=Math.max(z1,mesh[i+2]);
        }
        cx=(x0+x1)*0.5f; cy=(y0+y1)*0.5f; cz=(z0+z1)*0.5f;
        float maxDimension=Math.max(x1-x0,Math.max(y1-y0,z1-z0));
        scale=maxDimension>0f ? 2.15f/maxDimension : 1f;
        solid=direct(mesh);
        vertexCount=mesh.length/6;
        float[] wire=new float[mesh.length*2];
        int[] order={0,1,1,2,2,0};
        int out=0;
        for (int i=0;i<mesh.length;i+=18) {
            for (int n:order) {
                System.arraycopy(mesh,i+n*6,wire,out,6);
                out+=6;
            }
        }
        edges=direct(wire);
        edgeCount=wire.length/6;
        requestRender();
    }

    public void setShowEdges(boolean enabled) {
        queueEvent(() -> { showEdges=enabled; requestRender(); });
    }

    public void resetCamera() {
        queueEvent(() -> {
            rotX=-22f; rotY=-30f; distance=4.4f; panX=0; panY=0;
            requestRender();
        });
    }

    private static int compileShader(int kind,String source) {
        int shader=GLES20.glCreateShader(kind);
        GLES20.glShaderSource(shader,source);
        GLES20.glCompileShader(shader);
        int[] ok=new int[1];
        GLES20.glGetShaderiv(shader,GLES20.GL_COMPILE_STATUS,ok,0);
        if (ok[0]==0) {
            String message=GLES20.glGetShaderInfoLog(shader);
            GLES20.glDeleteShader(shader);
            throw new IllegalStateException("GL shader: "+message);
        }
        return shader;
    }

    @Override public void onSurfaceCreated(javax.microedition.khronos.opengles.GL10 gl,
                                           javax.microedition.khronos.egl.EGLConfig config) {
        GLES20.glClearColor(0.025f,0.05f,0.10f,1f);
        GLES20.glEnable(GLES20.GL_DEPTH_TEST);
        GLES20.glDisable(GLES20.GL_CULL_FACE);
        int vertex=compileShader(GLES20.GL_VERTEX_SHADER,VS);
        int fragment=compileShader(GLES20.GL_FRAGMENT_SHADER,FS);
        program=GLES20.glCreateProgram();
        GLES20.glAttachShader(program,vertex);
        GLES20.glAttachShader(program,fragment);
        GLES20.glLinkProgram(program);
        int[] status=new int[1];
        GLES20.glGetProgramiv(program,GLES20.GL_LINK_STATUS,status,0);
        GLES20.glDeleteShader(vertex);
        GLES20.glDeleteShader(fragment);
        if(status[0]==0) throw new IllegalStateException(GLES20.glGetProgramInfoLog(program));
    }

    @Override public void onSurfaceChanged(javax.microedition.khronos.opengles.GL10 gl,int w,int h) {
        viewportWidth=Math.max(1,w); viewportHeight=Math.max(1,h);
        GLES20.glViewport(0,0,viewportWidth,viewportHeight);
        Matrix.perspectiveM(projection,0,45f,(float)viewportWidth/viewportHeight,0.1f,100f);
    }

    private void draw(FloatBuffer buffer,int count,int mode,float edgeValue,int pos,int normal,int wireLocation) {
        if(buffer==null || count==0) return;
        GLES20.glUniform1f(wireLocation,edgeValue);
        buffer.position(0);
        GLES20.glEnableVertexAttribArray(pos);
        GLES20.glVertexAttribPointer(pos,3,GLES20.GL_FLOAT,false,STRIDE,buffer);
        buffer.position(3);
        GLES20.glEnableVertexAttribArray(normal);
        GLES20.glVertexAttribPointer(normal,3,GLES20.GL_FLOAT,false,STRIDE,buffer);
        GLES20.glDrawArrays(mode,0,count);
        GLES20.glDisableVertexAttribArray(pos);
        GLES20.glDisableVertexAttribArray(normal);
    }

    @Override public void onDrawFrame(javax.microedition.khronos.opengles.GL10 gl) {
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT|GLES20.GL_DEPTH_BUFFER_BIT);
        if(program==0 || solid==null || vertexCount==0) return;
        Matrix.setLookAtM(view,0,0,0,distance,0,0,0,0,1,0);
        Matrix.setIdentityM(model,0);
        Matrix.translateM(model,0,panX,panY,0f);
        Matrix.rotateM(model,0,rotX,1f,0f,0f);
        Matrix.rotateM(model,0,rotY,0f,1f,0f);
        Matrix.scaleM(model,0,scale,scale,scale);
        Matrix.translateM(model,0,-cx,-cy,-cz);
        Matrix.multiplyMM(vp,0,projection,0,view,0);
        Matrix.multiplyMM(mvp,0,vp,0,model,0);
        GLES20.glUseProgram(program);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(program,"uMVP"),1,false,mvp,0);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(program,"uModel"),1,false,model,0);
        int p=GLES20.glGetAttribLocation(program,"aPos");
        int n=GLES20.glGetAttribLocation(program,"aNormal");
        int wire=GLES20.glGetUniformLocation(program,"uEdges");
        if(showEdges) {
            GLES20.glEnable(GLES20.GL_POLYGON_OFFSET_FILL);
            GLES20.glPolygonOffset(1.0f,1.0f);
        }
        draw(solid,vertexCount,GLES20.GL_TRIANGLES,0f,p,n,wire);
        if(showEdges) {
            GLES20.glDisable(GLES20.GL_POLYGON_OFFSET_FILL);
            GLES20.glDepthMask(false);
            draw(edges,edgeCount,GLES20.GL_LINES,1f,p,n,wire);
            GLES20.glDepthMask(true);
        }
    }

    private static float touchSpan(MotionEvent e) {
        if(e.getPointerCount()<2) return 0f;
        return (float)Math.hypot(e.getX(1)-e.getX(0),e.getY(1)-e.getY(0));
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        int action=event.getActionMasked();
        if(action==MotionEvent.ACTION_DOWN) {
            prevX=event.getX(); prevY=event.getY(); twoFinger=false; return true;
        }
        if(action==MotionEvent.ACTION_POINTER_DOWN && event.getPointerCount()>=2) {
            prevX=(event.getX(0)+event.getX(1))*0.5f;
            prevY=(event.getY(0)+event.getY(1))*0.5f;
            prevSpan=touchSpan(event); twoFinger=true; return true;
        }
        if(action==MotionEvent.ACTION_MOVE) {
            boolean multi=event.getPointerCount()>=2;
            float x=multi?(event.getX(0)+event.getX(1))*0.5f:event.getX();
            float y=multi?(event.getY(0)+event.getY(1))*0.5f:event.getY();
            float dx=x-prevX,dy=y-prevY;
            float span=multi?touchSpan(event):0f;
            boolean wasTwo=twoFinger;
            float ratio=multi&&wasTwo&&prevSpan>1f?span/prevSpan:1f;
            queueEvent(() -> {
                if(multi) {
                    panX+=dx*3f/viewportHeight;
                    panY-=dy*3f/viewportHeight;
                    distance=Math.max(2.3f,Math.min(15f,
                            distance/Math.max(0.5f,Math.min(2f,ratio))));
                } else if(!wasTwo) {
                    rotX+=dy*0.6f;
                    rotY+=dx*0.6f;
                }
                requestRender();
            });
            prevX=x; prevY=y; prevSpan=span; twoFinger=multi; return true;
        }
        if(action==MotionEvent.ACTION_POINTER_UP) {
            int keep=event.getActionIndex()==0?1:0;
            prevX=event.getX(keep); prevY=event.getY(keep);
            twoFinger=false; return true;
        }
        return true;
    }
}
