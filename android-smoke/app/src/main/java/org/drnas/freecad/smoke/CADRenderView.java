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
    private static final int FLOAT_BYTES = 4;
    private static final int VERTEX_STRIDE = 6 * FLOAT_BYTES;
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
        "void main() {\n" +
        " vec3 n = normalize(vNormal);\n" +
        " vec3 l = normalize(vec3(0.6, 0.8, 1.0));\n" +
        " float diff = max(dot(n, l), 0.0);\n" +
        " float fill = max(dot(n, -l), 0.0) * 0.18;\n" +
        " vec3 steel = vec3(0.25, 0.57, 0.88);\n" +
        " vec3 color = steel * (0.31 + 0.63 * diff + fill) + vec3(0.035, 0.075, 0.12);\n" +
        " gl_FragColor = vec4(color, 1.0);\n" +
        "}\n";

    private final float[] projection = new float[16];
    private final float[] view = new float[16];
    private final float[] model = new float[16];
    private final float[] vp = new float[16];
    private final float[] mvp = new float[16];
    private FloatBuffer triangles;
    private int count = 0;
    private int shaderProgram = 0;
    private float rotX = -22f, rotY = -30f, distance = 5.0f, panX = 0, panY = 0;
    private float previousX, previousY, previousSpan;
    private boolean twoFinger = false;
    private int viewportWidth = 1, viewportHeight = 1;

    public CADRenderView(Context ctx) {
        super(ctx);
        setEGLContextClientVersion(2);
        setPreserveEGLContextOnPause(true);
        setRenderer(this);
        setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
    }

    // Must be invoked on the GL render thread.
    public void setTriangles(float[] data) {
        if (data == null || data.length < 18 || data.length % 18 != 0) {
            triangles = null;
            count = 0;
        } else {
            ByteBuffer bytes = ByteBuffer.allocateDirect(data.length * FLOAT_BYTES);
            bytes.order(ByteOrder.nativeOrder());
            triangles = bytes.asFloatBuffer();
            triangles.put(data);
            triangles.position(0);
            count = data.length / 6;
        }
        requestRender();
    }

    public void resetCamera() {
        queueEvent(() -> {
            rotX = -22f; rotY = -30f; distance = 5.0f; panX = 0; panY = 0;
            requestRender();
        });
    }

    private static int compileShader(int type, String source) {
        int shader = GLES20.glCreateShader(type);
        GLES20.glShaderSource(shader, source);
        GLES20.glCompileShader(shader);
        int[] ok = new int[1];
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, ok, 0);
        if (ok[0] == 0) {
            String log = GLES20.glGetShaderInfoLog(shader);
            GLES20.glDeleteShader(shader);
            throw new IllegalStateException("GL shader: " + log);
        }
        return shader;
    }

    @Override public void onSurfaceCreated(javax.microedition.khronos.opengles.GL10 gl,
                                           javax.microedition.khronos.egl.EGLConfig config) {
        GLES20.glClearColor(0.025f, 0.05f, 0.10f, 1.0f);
        GLES20.glEnable(GLES20.GL_DEPTH_TEST);
        GLES20.glDisable(GLES20.GL_CULL_FACE); // Also render the inner walls of the bore.
        int vertex = compileShader(GLES20.GL_VERTEX_SHADER, VS);
        int fragment = compileShader(GLES20.GL_FRAGMENT_SHADER, FS);
        shaderProgram = GLES20.glCreateProgram();
        GLES20.glAttachShader(shaderProgram, vertex);
        GLES20.glAttachShader(shaderProgram, fragment);
        GLES20.glLinkProgram(shaderProgram);
        int[] linked = new int[1];
        GLES20.glGetProgramiv(shaderProgram, GLES20.GL_LINK_STATUS, linked, 0);
        GLES20.glDeleteShader(vertex);
        GLES20.glDeleteShader(fragment);
        if (linked[0] == 0) throw new IllegalStateException(GLES20.glGetProgramInfoLog(shaderProgram));
    }

    @Override public void onSurfaceChanged(javax.microedition.khronos.opengles.GL10 gl,
                                           int width, int height) {
        viewportWidth = Math.max(1, width);
        viewportHeight = Math.max(1, height);
        GLES20.glViewport(0, 0, viewportWidth, viewportHeight);
        Matrix.perspectiveM(projection, 0, 45f, (float)viewportWidth / viewportHeight, 0.1f, 100f);
    }

    @Override public void onDrawFrame(javax.microedition.khronos.opengles.GL10 gl) {
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT | GLES20.GL_DEPTH_BUFFER_BIT);
        if (shaderProgram == 0 || triangles == null || count == 0) return;
        Matrix.setLookAtM(view, 0, 0f, 0f, distance, 0, 0, 0, 0, 1, 0);
        Matrix.setIdentityM(model, 0);
        Matrix.translateM(model, 0, panX, panY, 0);
        Matrix.rotateM(model, 0, rotX, 1f, 0f, 0f);
        Matrix.rotateM(model, 0, rotY, 0f, 1f, 0f);
        Matrix.scaleM(model, 0, 1f/17f, 1f/17f, 1f/17f);
        Matrix.translateM(model, 0, -5f, -10f, -15f);
        Matrix.multiplyMM(vp, 0, projection, 0, view, 0);
        Matrix.multiplyMM(mvp, 0, vp, 0, model, 0);

        GLES20.glUseProgram(shaderProgram);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(shaderProgram, "uMVP"),
                1, false, mvp, 0);
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(shaderProgram, "uModel"),
                1, false, model, 0);
        int pos = GLES20.glGetAttribLocation(shaderProgram, "aPos");
        int norm = GLES20.glGetAttribLocation(shaderProgram, "aNormal");
        triangles.position(0);
        GLES20.glEnableVertexAttribArray(pos);
        GLES20.glVertexAttribPointer(pos, 3, GLES20.GL_FLOAT, false, VERTEX_STRIDE, triangles);
        triangles.position(3);
        GLES20.glEnableVertexAttribArray(norm);
        GLES20.glVertexAttribPointer(norm, 3, GLES20.GL_FLOAT, false, VERTEX_STRIDE, triangles);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, count);
        GLES20.glDisableVertexAttribArray(pos);
        GLES20.glDisableVertexAttribArray(norm);
    }

    private static float span(MotionEvent event) {
        if (event.getPointerCount() < 2) return 0;
        return (float) Math.hypot(event.getX(1) - event.getX(0), event.getY(1) - event.getY(0));
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            previousX = event.getX(); previousY = event.getY();
            twoFinger = false;
            return true;
        }
        if (action == MotionEvent.ACTION_POINTER_DOWN && event.getPointerCount() == 2) {
            previousX = (event.getX(0) + event.getX(1)) * 0.5f;
            previousY = (event.getY(0) + event.getY(1)) * 0.5f;
            previousSpan = span(event);
            twoFinger = true;
            return true;
        }
        if (action == MotionEvent.ACTION_MOVE) {
            final boolean multi = event.getPointerCount() >= 2;
            float x = multi ? (event.getX(0)+event.getX(1))*0.5f : event.getX();
            float y = multi ? (event.getY(0)+event.getY(1))*0.5f : event.getY();
            float dx = x - previousX, dy = y - previousY;
            float newSpan = multi ? span(event) : 0;
            final boolean wasTwo = twoFinger;
            final float ratio = multi && wasTwo && previousSpan > 1f
                    ? newSpan / previousSpan : 1f;
            queueEvent(() -> {
                if (multi) {
                    panX += dx * 3f / viewportHeight;
                    panY -= dy * 3f / viewportHeight;
                    distance = Math.max(2.4f, Math.min(13f, distance / Math.max(0.5f, Math.min(2f, ratio))));
                } else if (!wasTwo) {
                    rotY += dx * 0.6f;
                    rotX += dy * 0.6f;
                }
                requestRender();
            });
            previousX = x; previousY = y; previousSpan = newSpan; twoFinger = multi;
            return true;
        }
        if (action == MotionEvent.ACTION_POINTER_UP) {
            // Rebase after the pointer transition; avoids camera jumps.
            int left = event.getActionIndex() == 0 ? 1 : 0;
            previousX = event.getX(left);
            previousY = event.getY(left);
            twoFinger = false;
            return true;
        }
        return true;
    }
}
