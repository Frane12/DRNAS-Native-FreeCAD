package org.drnas.freecad.smoke;

public final class NativeCAD {
    private NativeCAD() {}
    static {
        System.loadLibrary("drnas_occt_geometry");
        System.loadLibrary("drnas_freecadbase_probe");
        System.loadLibrary("drnas_freecad_smoke");
    }
    public static native String runGeometry();
    public static native String runFreeCADBase();
}
