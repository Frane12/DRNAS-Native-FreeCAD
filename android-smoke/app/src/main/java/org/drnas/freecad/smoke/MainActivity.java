package org.drnas.freecad.smoke;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import java.io.OutputStream;

public final class MainActivity extends Activity {
    private static final int SAVE_STL=4108;
    private static final String[] SHAPES={
        "Box + Ø6 hole  •  10×20×30 mm",
        "Solid box  •  20×20×20 mm",
        "Cylinder  •  Ø16×24 mm",
        "Sphere  •  Ø20 mm"
    };
    private CADRenderView canvas;
    private TextView report;
    private Spinner shapes;
    private float[] lastMesh;
    private int generation=0;
    private int chosenShape=0;
    private boolean ready=false;

    private TextView label(String content,int points,int tint) {
        TextView v=new TextView(this);
        v.setText(content);v.setTextSize(points);v.setTextColor(tint);
        return v;
    }
    private void status(String message) { runOnUiThread(() -> report.setText(message)); }
    private Button action(String name,LinearLayout row,Runnable task) {
        Button button=new Button(this);
        button.setAllCaps(false);button.setText(name);
        row.addView(button,new LinearLayout.LayoutParams(0,-2,1f));
        button.setOnClickListener(v -> task.run());
        return button;
    }

    private void refreshModel() {
        int choice=shapes.getSelectedItemPosition();
        if(choice<0 || choice>=SHAPES.length) choice=0;
        final int type=choice;
        final int ticket=++generation;
        ready=false;
        lastMesh=null;
        status("Creating native OCCT solid: "+SHAPES[type]);
        new Thread(() -> {
            try {
                float[] values=NativeCAD.createOcctMesh(type);
                runOnUiThread(() -> {
                    if(ticket!=generation) return;
                    if(values==null || values.length<18 || values.length%18!=0) {
                        status("FAIL: OCCT mesh generation. Change shape or reload.");
                        return;
                    }
                    lastMesh=values;
                    ready=true;
                    chosenShape=type;
                    canvas.queueEvent(() -> canvas.setTriangles(values));
                    report.setText("RENDER READY • "+values.length/18+" triangles\n"+
                        SHAPES[type]+"\n"+
                        "Rotate: 1 finger  •  Pan/zoom: 2 fingers\n"+
                        "Save STL exports this triangulated solid in mm.");
                });
            } catch(Throwable ex) {
                runOnUiThread(() -> {
                    if(ticket==generation) status("NATIVE MESH ERROR: "+android.util.Log.getStackTraceString(ex));
                });
            }
        },"drnas-native-solid").start();
    }

    private void saveStl() {
        if(!ready || lastMesh==null) {
            status("Wait for a valid mesh before exporting STL.");
            return;
        }
        Intent doc=new Intent(Intent.ACTION_CREATE_DOCUMENT);
        doc.addCategory(Intent.CATEGORY_OPENABLE);
        doc.setType("application/octet-stream");
        String[] names={"drnas_drilled_box.stl","drnas_box.stl","drnas_cylinder.stl","drnas_sphere.stl"};
        doc.putExtra(Intent.EXTRA_TITLE,names[chosenShape]);
        startActivityForResult(doc,SAVE_STL);
    }

    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request!=SAVE_STL || result!=RESULT_OK || data==null) return;
        Uri target=data.getData();
        float[] snapshot=lastMesh;
        if(target==null || !ready || snapshot==null) {
            status("STL export cancelled because model is not available.");return;
        }
        status("Writing binary STL to Android document…");
        new Thread(() -> {
            try(OutputStream output=getContentResolver().openOutputStream(target,"w")) {
                if(output==null) throw new IllegalStateException("Cannot open destination");
                int count=StlWriter.write(output,snapshot);
                status("STL SAVED: "+count+" triangles • dimensions in millimetres");
            } catch(Exception ex) {
                status("STL EXPORT FAILED: "+ex.getMessage());
            }
        },"drnas-export-stl").start();
    }

    private void nativeTest(boolean freecad) {
        status("Running existing ARM64 CAD test…");
        new Thread(() -> {
            try {
                String s=freecad?NativeCAD.runFreeCADBase():NativeCAD.runGeometry();
                status(s.replace("\\n","\n"));
            } catch(Throwable e) { status("NATIVE TEST ERROR: "+android.util.Log.getStackTraceString(e)); }
        },"drnas-cad-test").start();
    }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().setStatusBarColor(0xff081632);
        getWindow().setNavigationBarColor(0xff081632);
        LinearLayout root=new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff0a1b3a);
        root.setPadding(10,12,10,6);
        root.setFitsSystemWindows(true);
        TextView title=label("DRNAS FreeCAD • Native Core",21,Color.WHITE);
        title.setTypeface(null,Typeface.BOLD);
        root.addView(title);
        root.addView(label("FreeCADBase + FreeCADApp ARM64 • OCCT GPU viewer",12,0xff81bff1));

        canvas=new CADRenderView(this);
        root.addView(canvas,new LinearLayout.LayoutParams(-1,0,1f));

        LinearLayout select=new LinearLayout(this);
        select.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(select);
        shapes=new Spinner(this);
        ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,SHAPES);
        shapes.setAdapter(adapter);
        shapes.setBackgroundColor(0xffd3e6f7);
        select.addView(shapes,new LinearLayout.LayoutParams(0,-2,2.2f));
        action("Save STL",select,this::saveStl);
        shapes.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent,View view,int position,long id) {
                refreshModel();
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });

        LinearLayout navigation=new LinearLayout(this);
        navigation.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(navigation);
        action("Reload",navigation,this::refreshModel);
        action("Reset camera",navigation,() -> canvas.resetCamera());
        CheckBox wire=new CheckBox(this);
        wire.setText("Mesh edges");
        wire.setTextColor(Color.WHITE);
        navigation.addView(wire,new LinearLayout.LayoutParams(0,-2,1.3f));
        wire.setOnCheckedChangeListener((v,isChecked) -> canvas.setShowEdges(isChecked));

        LinearLayout tests=new LinearLayout(this);
        tests.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(tests);
        action("CAD test",tests,() -> nativeTest(false));
        action("Base source test",tests,() -> nativeTest(true));
        LinearLayout fullCoreRow=new LinearLayout(this);
        fullCoreRow.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(fullCoreRow);
        action("TEST REAL FREECAD CORE",fullCoreRow,() -> {
            status("Loading real FreeCADBase + FreeCADApp ARM64…");
            new Thread(() -> {
                String result=NativeCAD.checkFullFreeCADCore(getApplicationInfo().nativeLibraryDir);
                status(result);
            },"drnas-full-core-link-test").start();
        });

        report=label("Creating initial CAD shape…",12,0xffd6e9fd);
        report.setPadding(9,9,9,9);
        report.setBackgroundColor(0xff112c51);
        root.addView(report,new LinearLayout.LayoutParams(-1,-2));
        Button exit=new Button(this);
        exit.setAllCaps(false);exit.setText("Exit");
        exit.setOnClickListener(v -> finish());
        root.addView(exit,new LinearLayout.LayoutParams(-1,-2));
        setContentView(root);
    }
    @Override protected void onPause() { canvas.onPause();super.onPause(); }
    @Override protected void onResume() { super.onResume();if(canvas!=null) canvas.onResume(); }
}
