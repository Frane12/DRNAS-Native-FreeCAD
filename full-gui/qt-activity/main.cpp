#include <QApplication>
#include <QByteArray>
#include <QMainWindow>
#include <QString>
#include <dlfcn.h>

// This is a GENUINE FreeCAD shared-library integration diagnostic. Loading
// upstream .so binaries is not equivalent to starting its MainWindow:
// FreeCAD's desktop runApplication() constructs another QApplication and
// cannot be called inside QtActivity without an Android-specific refactor.
extern "C" void* drnas_create_qt_gui_shell_after_qt_android_bootstrap();

static QByteArray verifyUpstreamNativeLibraries()
{
    constexpr const char* libraries[] = {
        "libFreeCADBase.so", "libFreeCADApp.so", "libCoin.so",
        "libGL.so", "libGLU.so", "libFreeCADGui.so",
    };
    for (const char* library : libraries) {
        // RTLD_NOW exposes missing required symbols on the actual device,
        // not just at compile time. Qt6 is already initialized by QtActivity.
        if (!dlopen(library, RTLD_NOW | RTLD_GLOBAL)) {
            const char* cause = dlerror();
            return QStringLiteral("Native FreeCAD load FAILED: %1: %2")
                .arg(QString::fromLatin1(library))
                .arg(QString::fromLocal8Bit(cause ? cause : "unknown")).toUtf8();
        }
    }
    return QByteArray("PASS: genuine FreeCADBase / FreeCADApp / FreeCADGui / Coin3D / GL4ES loaded.");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);  // Qt Android Activity owns the event loop.
    qputenv("DRNAS_FREECAD_NATIVE_STATUS", verifyUpstreamNativeLibraries());
    auto* window = static_cast<QMainWindow*>(
        drnas_create_qt_gui_shell_after_qt_android_bootstrap());
    window->show();
    return app.exec();
}
