#include <jni.h>
#include <cstdio>
#include <dlfcn.h>
#include <string>
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

extern "C" int drnas_freecadbase_probe();

extern "C" JNIEXPORT jstring JNICALL
Java_org_drnas_freecad_smoke_NativeCAD_runFreeCADBase(JNIEnv* env, jclass) {
    const int rc = drnas_freecadbase_probe();
    char output[400];
    if (rc == 0) {
        std::snprintf(output, sizeof(output),
            "PASS: Original FreeCAD Base source running on Android ARM64!\n"
            "Base64 text + binary roundtrips: PASS\n"
            "Vector3D length, dot, cross, normalization: PASS\n"
            "This is a small FreeCADBase C++ source subset, not FreeCADApp or full FreeCADBase.");
    } else {
        std::snprintf(output, sizeof(output),
            "FAIL: FreeCADBase C++ source test returned code %d.", rc);
    }
    return env->NewStringUTF(output);
}

/*
 * Android's System.load() invokes JNI_OnLoad using dlsym() on the entire
 * dependency group. FreeCADApp itself does not declare JNI_OnLoad, but its
 * DT_NEEDED dependency Qt6Core does. Qt6Core's JNI_OnLoad expects the
 * official Qt Android bootstrap, and returns JNI_ERR in our regular Activity.
 *
 * For the initial *ELF linkage* smoke test, use Bionic dlopen() directly.
 * This resolves relocations and transitive dependencies without invoking
 * the transitive Qt6Core JNI_OnLoad. It intentionally does NOT initialize
 * FreeCAD documents, QtNative or the full GUI.
 */
extern "C" JNIEXPORT jstring JNICALL
Java_org_drnas_freecad_smoke_NativeCAD_nativeCheckFullFreeCADCore(JNIEnv* env,
                                                                  jclass,
                                                                  jstring javaLibDir) {
    if (javaLibDir == nullptr) {
        return env->NewStringUTF("FAIL: Android nativeLibraryDir unavailable");
    }
    const char* raw = env->GetStringUTFChars(javaLibDir, nullptr);
    if (!raw) return nullptr; // JNI OOME already pending
    const std::string dir(raw);
    env->ReleaseStringUTFChars(javaLibDir, raw);

    static void* baseHandle = nullptr;
    static void* appHandle = nullptr;
    std::string result = "REAL FREECAD ANDROID ARM64 CORE\n";
    struct Library { const char* name; void** handle; };
    const Library libs[] = {
        {"FreeCADBase", &baseHandle},
        {"FreeCADApp", &appHandle}
    };
    for (const auto& lib : libs) {
        const std::string path = dir + "/lib" + lib.name + ".so";
        if (!*lib.handle) {
            dlerror();
            *lib.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!*lib.handle) {
                const char* error = dlerror();
                result += "FAIL: lib";
                result += lib.name;
                result += ".so\n";
                result += (error ? error : "dlopen returned null with no error");
                return env->NewStringUTF(result.c_str());
            }
        }
        result += "PASS: lib";
        result += lib.name;
        result += ".so linked (Bionic dlopen)\n";
    }
    result += "Both real ARM64 core libraries resolved on-device.\n";
    result += "Qt JNI_OnLoad intentionally bypassed; runtime CAD documents, "
              "Qt bootstrap and desktop GUI are NOT yet verified.";
    return env->NewStringUTF(result.c_str());
}
