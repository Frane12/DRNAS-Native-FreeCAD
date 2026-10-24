package org.drnas.freecad.bootstrap;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.system.Os;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.List;

/** Lightweight splash; recovery is offered only for an actual recorded crash/ANR. */
public final class StartupActivity extends Activity {
    private boolean starting;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().getDecorView().setSystemUiVisibility(0x1706);
        if (hasUnseenCrash()) showRecovery(); else showSplash();
    }
    private boolean hasUnseenCrash() {
        if (Build.VERSION.SDK_INT < 30) return false;
        try {
            ActivityManager manager = (ActivityManager)getSystemService(ACTIVITY_SERVICE);
            List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(getPackageName(),0,1);
            if (exits.isEmpty()) return false;
            ApplicationExitInfo exit = exits.get(0);
            int reason = exit.getReason();
            boolean crashed = reason == ApplicationExitInfo.REASON_CRASH ||
                reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_ANR;
            SharedPreferences prefs = getSharedPreferences("drnas_recovery",MODE_PRIVATE);
            if (!crashed || exit.getTimestamp() <= prefs.getLong("shownCrash",0)) return false;
            prefs.edit().putLong("shownCrash",exit.getTimestamp()).apply();
            return true;
        } catch (RuntimeException error) { Log.w("DRNAS","Cannot read process exit history",error);return false; }
    }
    private LinearLayout page() {
        LinearLayout panel=new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(28),dp(24),dp(28),dp(24));
        panel.setGravity(Gravity.CENTER);
        panel.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
            new int[]{Color.rgb(9,21,35),Color.rgb(26,58,82)}));
        return panel;
    }
    private TextView text(String value,int size) {
        TextView text=new TextView(this);text.setText(value);text.setTextColor(Color.WHITE);
        text.setTextSize(size);text.setGravity(Gravity.CENTER);text.setPadding(0,dp(8),0,dp(8));return text;
    }
    private void showSplash() {
        LinearLayout panel=page();
        ImageView logo=new ImageView(this);
        int icon=getResources().getIdentifier("ic_drnas_freecad","drawable",getPackageName());
        if(icon!=0) logo.setImageResource(icon);
        panel.addView(logo,new LinearLayout.LayoutParams(dp(132),dp(132)));
        TextView title=text("DRNAS FreeCAD",30);title.setTypeface(Typeface.DEFAULT,Typeface.BOLD);panel.addView(title);
        TextView subtitle=text("Modeliraj. Stvaraj.",17);subtitle.setTextColor(Color.rgb(146,207,229));panel.addView(subtitle);
        ProgressBar progress=new ProgressBar(this);
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(dp(32),dp(32));params.topMargin=dp(22);
        panel.addView(progress,params);setContentView(panel);
        handler.postDelayed(() -> launch(true),650);
    }
    private String readLog(String name) {
        File file=new File(new File(getFilesDir(),"diagnostics"),name);
        if(!file.isFile())return "";
        try(RandomAccessFile input=new RandomAccessFile(file,"r")) {
            int length=(int)Math.min(input.length(),128*1024);
            byte[] bytes=new byte[length];input.seek(input.length()-length);input.readFully(bytes);
            return new String(bytes,StandardCharsets.UTF_8);
        } catch(java.io.IOException error) {return "Log nije dostupan: "+error.getMessage();}
    }
    private void showRecovery() {
        LinearLayout panel=page();panel.setGravity(Gravity.TOP);
        panel.addView(text("Prethodno pokretanje se srušilo",23));
        String report=readLog("current-launch.log");
        if(report.isEmpty())report=readLog("previous-launch.log");
        if(report.isEmpty())report="Nema spremljenog izvještaja. Pokreni sigurni način za dijagnostiku.";
        final String fullReport=report;
        ScrollView scroll=new ScrollView(this);TextView log=text(report,12);
        log.setGravity(Gravity.START);log.setTypeface(Typeface.MONOSPACE);log.setTextIsSelectable(true);
        scroll.addView(log);panel.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        Button copy=new Button(this);copy.setText("KOPIRAJ LOG");
        copy.setOnClickListener(v -> {ClipboardManager clipboard=(ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
            clipboard.setPrimaryClip(ClipData.newPlainText("DRNAS FreeCAD crash",fullReport));copy.setText("LOG KOPIRAN");});panel.addView(copy);
        Button start=new Button(this);start.setText("POKRENI FREECAD");start.setOnClickListener(v -> launch(true));panel.addView(start);
        Button safe=new Button(this);safe.setText("SIGURNI NAČIN");safe.setOnClickListener(v -> launch(false));panel.addView(safe);
        setContentView(panel);
    }
    private void launch(boolean automatic) {
        if(starting || isFinishing())return;
        try {
            Os.setenv("QT_SCALE_FACTOR","1.000000",true);
            Os.setenv("DRNAS_AUTOSTART",automatic?"1":"0",true);
            Os.setenv("DRNAS_VIEWPORT_AB","A",true);
            Os.setenv("DRNAS_ENABLE_COIN_REDRAW","1",true);
        } catch(android.system.ErrnoException error) {Log.e("DRNAS","Startup failed",error);return;}
        starting=true;
        Intent intent=new Intent();intent.setClassName(getPackageName(),"org.qtproject.qt.android.bindings.QtActivity");
        startActivity(intent);finish();
    }
    @Override protected void onDestroy() {handler.removeCallbacksAndMessages(null);super.onDestroy();}
}
