package org.drnas.freecad.smoke;

import android.app.Activity;
import android.os.Bundle;
import android.graphics.Color;
import android.graphics.Typeface;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private CADRenderView canvas;
    private TextView report;

    private TextView text(String s, int size, int color) {
        TextView v = new TextView(this);
        v.setText(s); v.setTextSize(size); v.setTextColor(color);
        return v;
    }

    private Button button(String title, LinearLayout parent, Runnable click) {
        Button b = new Button(this);
        b.setText(title);
        b.setAllCaps(false);
        parent.addView(b, new LinearLayout.LayoutParams(0, -2, 1f));
        b.setOnClickListener(v -> click.run());
        return b;
    }

    private void show(String message) { runOnUiThread(() -> report.setText(message)); }

    private void loadMesh() {
        show("Building OCCT B-Rep, Boolean cut and native triangle mesh…");
        new Thread(() -> {
            try {
                float[] floats = NativeCAD.createOcctMesh();
                if (floats == null || floats.length < 18 || floats.length % 18 != 0) {
                    show("FAIL: OCCT did not produce a triangle mesh. Previous CAD tests are still available.");
                    return;
                }
                // OpenGL buffer creation is done only on the GL thread.
                canvas.queueEvent(() -> canvas.setTriangles(floats));
                show("RENDER READY • " + (floats.length / 18) + " triangles\n" +
                    "Live geometry: box 10×20×30 mm, Ø6 mm through-hole\n" +
                    "Drag = rotate  |  Pinch = zoom  |  2 fingers = pan\n" +
                    "CAD kernel: OpenCASCADE Android ARM64");
            } catch (Throwable ex) {
                show("MESH ERROR: " + android.util.Log.getStackTraceString(ex));
            }
        }, "drnas-cad-meshing").start();
    }

    private void runNative(boolean freecad) {
        show("Running Android ARM64 native test…");
        new Thread(() -> {
            String answer;
            try {
                answer = freecad ? NativeCAD.runFreeCADBase() : NativeCAD.runGeometry();
            } catch (Throwable t) {
                answer = "NATIVE TEST ERROR: " + android.util.Log.getStackTraceString(t);
            }
            show(answer.replace("\\n", "\n"));
        }, "drnas-native-test").start();
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.rgb(8, 22, 50));
        getWindow().setNavigationBarColor(Color.rgb(8, 22, 50));
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(10, 27, 58));
        root.setPadding(10, 14, 10, 8);
        root.setFitsSystemWindows(true);
        TextView title = text("DRNAS FreeCAD • Render LAB", 22, Color.WHITE);
        title.setTypeface(null, Typeface.BOLD);
        root.addView(title);
        TextView subtitle = text("Native OCCT B-Rep → triangles → OpenGL ES 2.0", 13, 0xff81bff1);
        root.addView(subtitle);

        canvas = new CADRenderView(this);
        root.addView(canvas, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f));

        TextView hint = text("ONE FINGER: rotate     TWO: pan / pinch to zoom", 11, 0xff92b5d5);
        root.addView(hint);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(row);
        button("↻ Reload mesh", row, this::loadMesh);
        button("⌖ Reset view", row, () -> canvas.resetCamera());

        LinearLayout tests = new LinearLayout(this);
        tests.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(tests);
        button("CAD test", tests, () -> runNative(false));
        button("FreeCAD C++ test", tests, () -> runNative(true));

        report = text("Preparing native renderer…", 13, 0xffd6e9fd);
        report.setPadding(10, 10, 10, 10);
        report.setBackgroundColor(0xff112c51);
        root.addView(report, new LinearLayout.LayoutParams(-1, -2));
        Button exit = new Button(this);
        exit.setText("Exit");
        exit.setAllCaps(false);
        exit.setOnClickListener(v -> finish());
        root.addView(exit, new LinearLayout.LayoutParams(-1, -2));
        setContentView(root);
        loadMesh();
    }

    @Override protected void onPause() { canvas.onPause(); super.onPause(); }
    @Override protected void onResume() { super.onResume(); if (canvas != null) canvas.onResume(); }
}
