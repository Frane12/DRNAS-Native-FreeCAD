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
     * Linker smoke test for the complete upstream FreeCADBase/FreeCADApp
     * libraries, not a promise that the desktop GUI/document editor runs.
     * All dependent Android .so files are packaged beside them in nativeLibraryDir.
     */
    public static String checkFullFreeCADCore(String nativeLibraryDir) {
        String[] names={"FreeCADBase","FreeCADApp"};
        StringBuilder result=new StringBuilder("REAL FREECAD ANDROID ARM64 CORE\\n");
        for(String name:names) {
            try {
                System.load(nativeLibraryDir+"/lib"+name+".so");
                result.append("PASS: lib").append(name).append(".so loaded\\n");
            } catch(Throwable e) {
                return result.append("FAIL: lib").append(name).append(".so\\n")
                    .append(e.getClass().getSimpleName()).append(": ")
                    .append(e.getMessage()).toString();
            }
        }
        return result.append("Both core libraries load. This is still the Android "+
            "OCCT viewport with STL export, NOT FreeCAD's desktop GUI. "+
            "Parametric document operations require separate runtime tests.").toString();
    }
    public static native String runGeometry();
    public static native float[] createOcctMesh(int type);
    public static native String runFreeCADBase();
}
