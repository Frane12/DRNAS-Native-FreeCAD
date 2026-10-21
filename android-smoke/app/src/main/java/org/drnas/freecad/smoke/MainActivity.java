package org.drnas.freecad.smoke;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import java.io.OutputStream;

/**
 * DRNAS touch-first CAD viewport shell.
 *
 * The full upstream FreeCADBase and FreeCADApp binaries ship in this APK
 * and pass the user-verified Bionic dlopen check. Geometry displayed here
 * still comes from the working native OpenCASCADE JNI bridge; a true FreeCAD
 * document/view-provider adapter and desktop FreeCADGui are future stages.
 */
public final class MainActivity extends Activity {
    private static final int SAVE_STL=4108;
    private static final int NAVY=0xff0b1730;
    private static final int PANEL=0xff122640;
    private static final int WHITE=0xfff2f7ff;
    private static final int MUTED=0xff99b9d5;
    private static final int HIGHLIGHT=0xff47a9ed;
    private static final String[] SHAPES={
        "Drilled box  10×20×30",
        "Solid box  20×20×20",
        "Cylinder  Ø16×24",
        "Sphere  Ø20"
    };
    private static final String[] NAMES={
        "Drilled box", "Solid box", "Cylinder", "Sphere"
    };

    private CADRenderView canvas;
    private Spinner shapes;
    private TextView report, viewportBadge, propertyInfo, objectName, projectionText;
    private LinearLayout modelDrawer, engineDrawer, displayRow;
    private float[] lastMesh;
    private int chosenShape=0, generation=0;
    private boolean ready=false, gridEnabled=true, edgesEnabled=false, orthoEnabled=false;

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
    private GradientDrawable bg(int color,int radiusDp) {
        GradientDrawable g=new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(radiusDp));
        return g;
    }
    private TextView text(String value,float size,int color,boolean bold) {
        TextView v=new TextView(this);
        v.setText(value);
        v.setTextSize(size);
        v.setTextColor(color);
        if(bold) v.setTypeface(null,Typeface.BOLD);
        v.setGravity(Gravity.CENTER_VERTICAL);
        return v;
    }
    private LinearLayout column() {
        LinearLayout ll=new LinearLayout(this);
        ll.setOrientation(LinearLayout.VERTICAL);
        return ll;
    }
    private LinearLayout row() {
        LinearLayout ll=new LinearLayout(this);
        ll.setOrientation(LinearLayout.HORIZONTAL);
        ll.setGravity(Gravity.CENTER_VERTICAL);
        return ll;
    }
    private Button button(String title,int bgColor,Runnable action) {
        Button b=new Button(this);
        b.setText(title);
        b.setTextSize(12);
        b.setAllCaps(false);
        b.setTextColor(WHITE);
        b.setMinHeight(0); b.setMinimumHeight(0);
        b.setMinWidth(0); b.setMinimumWidth(0);
        b.setPadding(dp(9),0,dp(9),0);
        b.setBackground(bg(bgColor,8));
        b.setOnClickListener(v -> action.run());
        return b;
    }
    private void attach(LinearLayout layout,View v,int width,int height,int margin) {
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(width,height);
        p.setMargins(dp(margin),dp(margin),dp(margin),dp(margin));
        layout.addView(v,p);
    }
    private void state(String message) {
        runOnUiThread(() -> { if(report!=null) report.setText(message); });
    }
    private void setCamera(int preset,String label) {
        canvas.setCameraPreset(preset);
        viewportBadge.setText("3D VIEW  •  "+label);
    }
    private void refreshModel() {
        if(shapes==null || canvas==null || report==null) return;
        int pos=shapes.getSelectedItemPosition();
        if(pos<0 || pos>=SHAPES.length) pos=0;
        final int type=pos, ticket=++generation;
        ready=false;
        lastMesh=null;
        state("Generating native OpenCASCADE shape…");
        if(objectName!=null) objectName.setText("▣  "+NAMES[type]+"  (OCCT)");
        if(propertyInfo!=null) propertyInfo.setText("Generating B-Rep mesh…");
        new Thread(() -> {
            try {
                float[] mesh=NativeCAD.createOcctMesh(type);
                runOnUiThread(() -> {
                    if(ticket!=generation) return;
                    if(mesh==null || mesh.length<18 || mesh.length%18!=0) {
                        state("ERROR: native OCCT mesh failed. Tap Reload.");
                        return;
                    }
                    lastMesh=mesh;
                    ready=true;
                    chosenShape=type;
                    canvas.queueEvent(() -> canvas.setTriangles(mesh));
                    String count=(mesh.length/18)+" triangles";
                    state(NAMES[type]+"  •  "+count+"  •  mm");
                    if(propertyInfo!=null) propertyInfo.setText(
                            "Source: native OpenCASCADE\n"+
                            "Geometry: triangulated B-Rep\n"+
                            "Shape: "+SHAPES[type]+"\n"+
                            "Triangles: "+mesh.length/18+"\n"+
                            "Export: binary STL (mm)\n\n"+
                            "A FreeCAD document tree is not yet connected.");
                });
            } catch(Throwable e) {
                runOnUiThread(() -> {
                    if(ticket==generation) state("MESH ERROR: "+e.getMessage());
                });
            }
        },"drnas-viewport-meshing").start();
    }
    private void saveStl() {
        if(!ready || lastMesh==null) {
            state("Generate a valid shape before saving STL.");
            return;
        }
        Intent doc=new Intent(Intent.ACTION_CREATE_DOCUMENT);
        doc.addCategory(Intent.CATEGORY_OPENABLE);
        doc.setType("application/octet-stream");
        String[] files={"drnas_drilled_box.stl","drnas_box.stl",
                        "drnas_cylinder.stl","drnas_sphere.stl"};
        doc.putExtra(Intent.EXTRA_TITLE,files[chosenShape]);
        startActivityForResult(doc,SAVE_STL);
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request!=SAVE_STL || result!=RESULT_OK || data==null) return;
        Uri uri=data.getData();
        float[] snapshot=lastMesh;
        if(uri==null || !ready || snapshot==null) {
            state("STL export cancelled.");return;
        }
        state("Saving binary STL…");
        new Thread(() -> {
            try(OutputStream out=getContentResolver().openOutputStream(uri,"w")) {
                if(out==null) throw new IllegalStateException("No document stream");
                int triangles=StlWriter.write(out,snapshot);
                state("Saved STL: "+triangles+" triangles.");
            } catch(Exception e) {
                state("STL ERROR: "+e.getMessage());
            }
        },"drnas-export-stl").start();
    }
    private void toggleDrawer(LinearLayout drawer) {
        boolean show=drawer.getVisibility()!=View.VISIBLE;
        modelDrawer.setVisibility(View.GONE);
        engineDrawer.setVisibility(View.GONE);
        drawer.setVisibility(show?View.VISIBLE:View.GONE);
    }
    private void runEngineTest(boolean fullCore) {
        engineDrawer.setVisibility(View.VISIBLE);
        state("Testing native ARM64…");
        new Thread(() -> {
            try {
                String result=fullCore
                    ? NativeCAD.checkFullFreeCADCore(getApplicationInfo().nativeLibraryDir)
                    : NativeCAD.runFreeCADBase();
                state(result);
            } catch(Throwable e) {
                state("ENGINE TEST: "+e.getClass().getSimpleName()+": "+e.getMessage());
            }
        },"drnas-engine-test").start();
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().setStatusBarColor(0xff0a1831);
        getWindow().setNavigationBarColor(0xff09162b);
        LinearLayout root=column();
        root.setFitsSystemWindows(true);
        root.setBackgroundColor(NAVY);

        // Small, real mobile CAD toolbar: leave the majority of the screen
        // available to the actual OpenGL ES model rather than diagnostics.
        LinearLayout header=row();
        header.setPadding(dp(12),dp(5),dp(6),dp(4));
        root.addView(header,new LinearLayout.LayoutParams(-1,dp(51)));
        LinearLayout branding=column();
        TextView title=text("DRNAS  /  FREECAD",17,WHITE,true);
        branding.addView(title);
        branding.addView(text("Native ARM64  •  3D viewport preview",10,MUTED,false));
        header.addView(branding,new LinearLayout.LayoutParams(0,-2,1f));
        Button model=button("Model",PANEL,() -> toggleDrawer(modelDrawer));
        attach(header,model,dp(58),dp(35),2);
        Button engine=button("Engine",PANEL,() -> toggleDrawer(engineDrawer));
        attach(header,engine,dp(61),dp(35),2);
        Button exit=button("✕",0xff274465,this::finish);
        attach(header,exit,dp(32),dp(35),2);

        // The viewport is the expandable center of the app.
        FrameLayout viewport=new FrameLayout(this);
        root.addView(viewport,new LinearLayout.LayoutParams(-1,0,1f));
        canvas=new CADRenderView(this);
        viewport.addView(canvas,new FrameLayout.LayoutParams(-1,-1));

        LinearLayout overlay=column();
        overlay.setPadding(dp(7),dp(7),dp(7),0);
        FrameLayout.LayoutParams op=new FrameLayout.LayoutParams(-1,-2,Gravity.TOP);
        viewport.addView(overlay,op);
        LinearLayout camera=row();
        camera.setBackground(bg(0xd911263f,9));
        camera.setPadding(dp(4),dp(3),dp(4),dp(3));
        overlay.addView(camera,new LinearLayout.LayoutParams(-1,dp(44)));
        viewportBadge=text("3D VIEW  •  ISO",11,WHITE,true);
        viewportBadge.setPadding(dp(5),0,0,0);
        camera.addView(viewportBadge,new LinearLayout.LayoutParams(0,-1,1f));
        for(int i=0;i<4;i++){
            final int preset=i;
            String[] labels={"ISO","TOP","FRONT","RIGHT"};
            Button b=button(labels[i],0xff254562,() -> setCamera(preset,labels[preset]));
            b.setTextSize(10);
            LinearLayout.LayoutParams bp=new LinearLayout.LayoutParams(dp(51),dp(32));
            bp.setMargins(dp(2),0,0,0);
            camera.addView(b,bp);
        }

        TextView guide=text("Orbit: drag  •  Pan: 2 fingers  •  Zoom: pinch  •  Fit: double tap",
                10,0xffc0d8ea,false);
        guide.setPadding(dp(9),dp(5),dp(9),dp(5));
        guide.setBackground(bg(0xd018304d,9));
        FrameLayout.LayoutParams gp=new FrameLayout.LayoutParams(-2,-2,
                Gravity.BOTTOM|Gravity.CENTER_HORIZONTAL);
        gp.setMargins(dp(6),0,dp(6),dp(10));
        viewport.addView(guide,gp);

        // A functional object/selection inspector for the OCCT preview mesh.
        // It does not misrepresent a CAD object as a real FreeCAD Document.
        modelDrawer=column();
        modelDrawer.setPadding(dp(12),dp(10),dp(12),dp(12));
        modelDrawer.setBackground(bg(0xf0102946,10));
        modelDrawer.setVisibility(View.GONE);
        TextView treeTitle=text("MODEL  •  Preview",14,WHITE,true);
        modelDrawer.addView(treeTitle);
        modelDrawer.addView(text("▾  OCCT shape preview",12,MUTED,false));
        objectName=text("▣  Drilled box  (OCCT)",12,WHITE,true);
        objectName.setPadding(dp(10),dp(6),0,dp(8));
        modelDrawer.addView(objectName);
        propertyInfo=text("Native B-Rep geometry · mm",11,MUTED,false);
        modelDrawer.addView(propertyInfo);
        FrameLayout.LayoutParams mp=new FrameLayout.LayoutParams(dp(244),-2,
                Gravity.TOP|Gravity.LEFT);
        mp.setMargins(dp(10),dp(59),0,0);
        viewport.addView(modelDrawer,mp);

        engineDrawer=column();
        engineDrawer.setPadding(dp(12),dp(10),dp(12),dp(10));
        engineDrawer.setBackground(bg(0xf0122946,10));
        engineDrawer.setVisibility(View.GONE);
        engineDrawer.addView(text("UPSTREAM FREECAD CORE",13,WHITE,true));
        engineDrawer.addView(text("Bionic dlopen test — no FreeCADGui yet",10,MUTED,false));
        LinearLayout testRow=row();
        engineDrawer.addView(testRow);
        attach(testRow,button("Test Base + App",0xff216395,
                () -> runEngineTest(true)),dp(138),dp(35),3);
        attach(testRow,button("Base C++ test",0xff254562,
                () -> runEngineTest(false)),dp(112),dp(35),3);
        FrameLayout.LayoutParams ep=new FrameLayout.LayoutParams(-2,-2,
                Gravity.TOP|Gravity.RIGHT);
        ep.setMargins(dp(8),dp(59),dp(8),0);
        viewport.addView(engineDrawer,ep);

        LinearLayout tools=column();
        tools.setPadding(dp(8),dp(4),dp(8),dp(5));
        tools.setBackgroundColor(PANEL);
        root.addView(tools,new LinearLayout.LayoutParams(-1,-2));
        LinearLayout fileRow=row();
        tools.addView(fileRow,new LinearLayout.LayoutParams(-1,dp(43)));
        shapes=new Spinner(this);
        ArrayAdapter<String> adapter=new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item,SHAPES);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        shapes.setAdapter(adapter);
        shapes.setBackground(bg(0xffd7e8f8,7));
        LinearLayout.LayoutParams spinnerParams=new LinearLayout.LayoutParams(0,dp(39),1f);
        spinnerParams.setMargins(0,0,dp(6),0);
        fileRow.addView(shapes,spinnerParams);
        attach(fileRow,button("SAVE STL",0xff256da0,this::saveStl),dp(96),dp(39),0);

        LinearLayout actions=row();
        tools.addView(actions,new LinearLayout.LayoutParams(-1,dp(42)));
        Button fit=button("FIT",0xff254562,() -> canvas.fitModel());
        actions.addView(fit,new LinearLayout.LayoutParams(0,dp(34),1f));
        Button projection=button("ORTHO",0xff254562,() -> {
            orthoEnabled=!orthoEnabled;
            canvas.setOrthographic(orthoEnabled);
            projectionText.setText(orthoEnabled?"PERSPECTIVE":"ORTHO");
        });
        projectionText=projection;
        LinearLayout.LayoutParams part=new LinearLayout.LayoutParams(0,dp(34),1.3f);
        part.setMargins(dp(5),0,0,0);
        actions.addView(projection,part);
        Button grid=button("GRID ON",0xff256da0,() -> {
            gridEnabled=!gridEnabled;
            canvas.setShowGrid(gridEnabled);
        });
        part=new LinearLayout.LayoutParams(0,dp(34),1.1f);
        part.setMargins(dp(5),0,0,0);
        actions.addView(grid,part);
        grid.setOnClickListener(v -> {
            gridEnabled=!gridEnabled;
            canvas.setShowGrid(gridEnabled);
            grid.setText(gridEnabled?"GRID ON":"GRID OFF");
        });
        Button edges=button("EDGES OFF",0xff254562,() -> {});
        part=new LinearLayout.LayoutParams(0,dp(34),1.3f);
        part.setMargins(dp(5),0,0,0);
        actions.addView(edges,part);
        edges.setOnClickListener(v -> {
            edgesEnabled=!edgesEnabled;
            canvas.setShowEdges(edgesEnabled);
            edges.setText(edgesEnabled?"EDGES ON":"EDGES OFF");
        });

        report=text("Loading native CAD viewport…",11,0xffb9d5eb,false);
        report.setSingleLine(false);
        report.setMaxLines(4);
        report.setPadding(dp(7),dp(6),dp(7),dp(2));
        tools.addView(report,new LinearLayout.LayoutParams(-1,-2));
        setContentView(root);
        shapes.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent,View view,
                                                 int position,long id) {
                refreshModel();
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
        refreshModel();
    }
    @Override protected void onPause() {
        if(canvas!=null) canvas.onPause();
        super.onPause();
    }
    @Override protected void onResume() {
        super.onResume();
        if(canvas!=null) canvas.onResume();
    }
}
