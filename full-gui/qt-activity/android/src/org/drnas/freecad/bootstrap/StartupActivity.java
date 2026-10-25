package org.drnas.freecad.bootstrap;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.system.Os;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.Locale;

/** Runs before Qt starts, so display scaling and touch coordinates agree. */
public final class StartupActivity extends Activity {
    private static final int PC_DESKTOP = -1024;
    private static final int DESKTOP_800 = -800, DESKTOP_640 = -640;
    private static final int[] LONG_EDGES = {DESKTOP_800, DESKTOP_640, PC_DESKTOP, 0, 960, 1280, 1600, 1920};
    private static final String PREFS = "drnas_startup_display";
    private RadioGroup choices;
    private RadioGroup viewportTests;
    private SharedPreferences preferences;
    private DisplayMetrics metrics;
    private boolean starting;

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        preferences = getSharedPreferences(PREFS, MODE_PRIVATE);
        metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        // Migrate the old tiny 1024x768 test once to the larger 800x600 UI.
        // Subsequent launches preserve the explicit user selection.
        int saved = preferences.getBoolean("touchLandscapeChoiceSaved", false)
                ? preferences.getInt("longEdge", DESKTOP_800) : DESKTOP_800;
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(20), dp(16), dp(20), dp(20));
        panel.setBackgroundColor(Color.rgb(17, 31, 49));
        scroll.addView(panel);
        TextView title = new TextView(this);
        title.setText("DRNAS FreeCAD");
        title.setTextColor(Color.WHITE);
        title.setTextSize(26);
        panel.addView(title);
        TextView help = new TextView(this);
        help.setText("FreeCAD s Android rasporedom i izvornim ikonama. "
                + "Osnovne naredbe su gore, a alate odabrane grupe možeš pomicati prstom. "
                + "Model koristi punu širinu zaslona.\n\n"
                + "Nakon pokretanja automatski se otvara Part Design.");
        help.setTextColor(Color.rgb(215, 232, 245));
        help.setTextSize(17);
        help.setPadding(0, dp(12), 0, dp(8));
        panel.addView(help);
        choices = new RadioGroup(this);
        int nativeWidth = Math.round(Math.max(metrics.widthPixels, metrics.heightPixels) / metrics.density);
        int nativeHeight = Math.round(Math.min(metrics.widthPixels, metrics.heightPixels) / metrics.density);
        for (int edge : LONG_EDGES) {
            RadioButton radio = new RadioButton(this);
            radio.setId(View.generateViewId());
            radio.setTag(edge);
            int width = nativeWidth;
            int height = nativeHeight;
            if (edge < 0) {
                width = -edge;
                height = width * 3 / 4;
            } else if (edge != 0) {
                double ratio = (double) edge / Math.max(nativeWidth, nativeHeight);
                width = (int) Math.round(nativeWidth * ratio);
                height = (int) Math.round(nativeHeight * ratio);
            }
            radio.setText(edge < 0 ? (edge == DESKTOP_800 ? "Android — standardne kontrole (preporučeno)" :
                    edge == DESKTOP_640 ? "Android — veće kontrole" : "Android — manje kontrole") :
                    (edge == 0 ? "Automatski — " : "") + width + " × " + height);
            radio.setTextSize(19);
            radio.setTextColor(Color.WHITE);
            radio.setButtonTintList(android.content.res.ColorStateList.valueOf(Color.rgb(110, 205, 255)));
            radio.setMinHeight(dp(56));
            choices.addView(radio);
            if (edge == saved) radio.setChecked(true);
        }
        if (choices.getCheckedRadioButtonId() == -1) {
            ((RadioButton) choices.getChildAt(0)).setChecked(true);
        }
        panel.addView(choices);
        TextView testHelp = new TextView(this);
        testHelp.setText("Proba prikaza — odaberi prije pokretanja");
        testHelp.setTextColor(Color.rgb(215, 232, 245));
        testHelp.setTextSize(17);
        testHelp.setPadding(0, dp(12), 0, dp(4));
        panel.addView(testHelp);
        viewportTests = new RadioGroup(this);
        for (String mode : new String[]{"A", "B"}) {
            RadioButton radio = new RadioButton(this);
            radio.setId(View.generateViewId());
            radio.setTag(mode);
            radio.setText(mode.equals("A") ? "TEST A — postojeći izračun" : "TEST B — dimenzije GL prikaza");
            radio.setTextColor(Color.WHITE);
            radio.setTextSize(17);
            radio.setMinHeight(dp(48));
            radio.setButtonTintList(android.content.res.ColorStateList.valueOf(Color.rgb(110, 205, 255)));
            viewportTests.addView(radio);
            if (mode.equals("A")) radio.setChecked(true);
        }
        panel.addView(viewportTests);
        Button start = new Button(this);
        start.setText("POKRENI PART DESIGN");
        start.setTextSize(19);
        start.setMinHeight(dp(64));
        start.setOnClickListener(v -> launch(true));
        panel.addView(start);
        Button safe = new Button(this);
        safe.setText("SIGURNI NAČIN / COPY LOG");
        safe.setTextSize(17);
        safe.setMinHeight(dp(56));
        safe.setOnClickListener(v -> launch(false));
        panel.addView(safe);
        setContentView(scroll);
    }

    private void launch(boolean automatic) {
        if (starting) return;
        RadioButton selected = findViewById(choices.getCheckedRadioButtonId());
        int edge = (Integer) selected.getTag();
        double logicalLong = Math.max(metrics.widthPixels, metrics.heightPixels) / (double) metrics.density;
        // Normalize the short physical edge to the selected logical height.
        // Qt applies this scale before both painting and native touch delivery.
        // A centered 4:3 CAD viewport fits without
        // nonuniform scaling; side gutters preserve the screen aspect ratio.
        double scale = edge < 0
                ? (Math.min(metrics.widthPixels, metrics.heightPixels) / metrics.density) / (-edge * 3.0 / 4.0)
                : (edge == 0 ? 1.0 : logicalLong / edge);
        // Android's Os.setenv updates the native process environment before
        // QtActivity loads Qt. No root, display-mode override or extra GL surface.
        try {
            Os.setenv("QT_SCALE_FACTOR", String.format(Locale.ROOT, "%.6f", scale), true);
            Os.setenv("DRNAS_AUTOSTART", automatic ? "1" : "0", true);
            Os.setenv("DRNAS_PC_DESKTOP_MODE", edge < 0 ? "1" : "0", true);
            Os.setenv("DRNAS_DESKTOP_WIDTH", edge < 0 ? Integer.toString(-edge) : "0", true);
            Os.setenv("DRNAS_ANDROID_COMMAND_UI", "1", true);
            RadioButton test = findViewById(viewportTests.getCheckedRadioButtonId());
            Os.setenv("DRNAS_VIEWPORT_AB", (String) test.getTag(), true);
            Os.setenv("DRNAS_ENABLE_COIN_REDRAW", "0", true);
        } catch (android.system.ErrnoException error) {
            Log.e("DRNAS", "Startup display configuration failed", error);
            return;
        }
        preferences.edit().putInt("longEdge", edge)
                .putBoolean("touchLandscapeChoiceSaved", true).apply();
        starting = true;
        Intent intent = new Intent();
        intent.setClassName(getPackageName(), edge < 0
                ? "org.drnas.freecad.bootstrap.DesktopQtActivity"
                : "org.qtproject.qt.android.bindings.QtActivity");
        startActivity(intent);
        finish();
    }
}
