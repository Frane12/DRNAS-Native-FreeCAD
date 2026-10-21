package org.drnas.freecad.smoke;
public final class NativeCAD {
    private NativeCAD() {}
    static {
        System.loadLibrary("drnas_occt_geometry");
        System.loadLibrary("drnas_freecadbase_probe");
        System.loadLibrary("drnas_freecad_smoke");
        System.loadLibrary("drnas_occt_mesh");
    }
    /**
     * Real FreeCAD Base/App on-device ELF linkage smoke test via Bionic dlopen.
     * Avoids Android's System.load calling Qt6Core's inherited JNI_OnLoad,
     * which requires QtActivity initialization. Does NOT yet run FreeCAD docs.
     */
    public static String checkFullFreeCADCore(String nativeLibraryDir) {
        return nativeCheckFullFreeCADCore(nativeLibraryDir);
    }
    private static native String nativeCheckFullFreeCADCore(String nativeLibraryDir);
    public static native String runGeometry();
    public static native float[] createOcctMesh(int type);
    public static native String runFreeCADBase();
}
