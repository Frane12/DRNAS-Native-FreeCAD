package org.drnas.freecad.smoke;

import android.app.Activity;
import android.os.Bundle;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private TextView output;

    private String performNativeTest() {
        try {
            return NativeCAD.runGeometry();
        } catch (Throwable t) {
            return "NATIVE LOAD / EXECUTION FAILED\\n" + android.util.Log.getStackTraceString(t);
        }
    }

    private void runTest() {
        output.setText("Running Open CASCADE solid modelling on Android ARM64…");
        new Thread(() -> {
            final String status = performNativeTest();
            runOnUiThread(() -> output.setText(status));
        }, "drnas-occt-test").start();
    }

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(32, 40, 32, 32);
        root.setFitsSystemWindows(true);
        scroll.addView(root, new ScrollView.LayoutParams(-1, -1));

        TextView title = new TextView(this);
        title.setText("DRNAS Native CAD • ARM64");
        title.setTypeface(null, Typeface.BOLD);
        title.setTextSize(24);
        root.addView(title);

        TextView note = new TextView(this);
        note.setText("\nNative Open CASCADE geometry smoke test.\n"
            + "Box 10×20×30 mm minus a 6 mm cylindrical hole.\n\n"
            + "This verifies our Android geometry bridge. "
            + "It is NOT yet the FreeCAD application or its GUI.\n");
        note.setTextSize(16);
        root.addView(note);

        Button run = new Button(this);
        run.setText("RUN CAD GEOMETRY TEST");
        root.addView(run);
        run.setOnClickListener(v -> runTest());

        output = new TextView(this);
        output.setTextSize(17);
        output.setTextIsSelectable(true);
        output.setPadding(0, 22, 0, 22);
        root.addView(output);

        Button exit = new Button(this);
        exit.setText("EXIT");
        root.addView(exit);
        exit.setOnClickListener(v -> finish());
        setContentView(scroll);
        runTest();
    }
}
