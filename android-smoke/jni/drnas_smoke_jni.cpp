#include <jni.h>
#include <cstdio>
#include "drnas_occt_geometry.h"

extern "C" JNIEXPORT jstring JNICALL
Java_org_drnas_freecad_smoke_NativeCAD_runGeometry(JNIEnv* env, jclass) {
    double volume = 0.0;
    const int code = drnas_occt_geometry_selftest(&volume);
    char buffer[384];
    if (code == 0) {
        std::snprintf(buffer, sizeof(buffer),
                      "PASS: Android ARM64 Open CASCADE performed solid modelling.\n"
                      "Box - cylindrical through-hole\nVolume: %.3f mm3\n"
                      "B-Rep validity: PASS\nMeshing: PASS\n"
                      "Next: FreeCAD core and document modules.", volume);
    } else {
        std::snprintf(buffer, sizeof(buffer),
                      "FAIL: Open CASCADE geometry test returned code %d.\n"
                      "The native library loaded, but CAD operations need debugging.", code);
    }
    return env->NewStringUTF(buffer);
}
