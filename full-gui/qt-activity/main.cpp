#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QSettings>

// Safe Android native bootstrap. Do NOT dlopen FreeCADGui/Coin3D here.
// Loading untested native DSOs on the startup thread can abort before the
// activity can display any diagnostics. The Qt GUI exposes an opt-in test
// that persists a breadcrumb immediately BEFORE each individual dlopen.
extern "C" void* drnas_create_qt_gui_shell_after_qt_android_bootstrap();

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("DRNAS"));
    QCoreApplication::setApplicationName(QStringLiteral("FreeCADNativeProbe"));
    QApplication app(argc, argv);
    // Do not clear QSettings "nativeProbe/pending" from a previous crash.
    // The next launch can display the exact native library being tested.
    auto* window = static_cast<QMainWindow*>(
        drnas_create_qt_gui_shell_after_qt_android_bootstrap());
    window->show();
    return app.exec();
}
