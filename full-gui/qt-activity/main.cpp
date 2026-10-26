#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QSettings>
#include "StartupDiagnostics.h"
#include "DrnasDebugCenter.h"
#include "LiveGuiLab.h"
#include "DrnasRenderSurfaceGuard.h"

// Safe Android native bootstrap. Do NOT dlopen FreeCADGui/Coin3D here.
// Loading untested native DSOs on the startup thread can abort before the
// activity can display any diagnostics. The Qt GUI exposes an opt-in test
// that persists a breadcrumb immediately BEFORE each individual dlopen.
extern "C" void* drnas_create_qt_gui_shell_after_qt_android_bootstrap();

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("DRNAS"));
    QCoreApplication::setApplicationName(QStringLiteral("FreeCADNativeProbe"));
#ifdef Q_OS_ANDROID
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // FreeCAD's QMenu/Qt::Popup must not force a second Android native window.
    // Qt 6.8 can draw menus in the existing QtActivity window when supported.
    // Keep this before QApplication construction, without touching Coin3D.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuWindows, true);
#endif
#endif
    QApplication app(argc, argv);
    // Begin private, persistent startup logging before any FreeCAD dlopen.
    DrnasDiagnostics::start();
#ifdef Q_OS_ANDROID
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    DrnasDiagnostics::note(QStringLiteral("MENU_POPUP: Qt 6.8 native menu windows opt-out requested"));
#endif
#endif
    DrnasDiagnostics::note(QStringLiteral("Creating Android GUI bootstrap shell"));
    // Do not clear QSettings "nativeProbe/pending" from a previous crash.
    // The next launch can display the exact native library being tested.
    auto* window = static_cast<QMainWindow*>(
        drnas_create_qt_gui_shell_after_qt_android_bootstrap());
    // Install before first show and before any document introduces OpenGL.
#ifdef DRNAS_FREECAD_FULL_INTEGRATION
    new DrnasRenderSurfaceGuard(window);
#endif
    window->show();
    // Always available in Safe Mode, before any risky native FreeCAD dlopen.
    // A child widget overlay only: never creates another Android surface.
    new DrnasDebugCenter(window);
#ifdef DRNAS_FREECAD_FULL_INTEGRATION
    new DrnasLiveGuiLab(window);
#endif
    DrnasDiagnostics::note(QStringLiteral("Qt GUI visible; entering event loop"));
    return app.exec();
}
