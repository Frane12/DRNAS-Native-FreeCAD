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
    QApplication app(argc, argv);
#ifdef DRNAS_FREECAD_FULL_INTEGRATION
    QSettings workspaceSettings;
    if(workspaceSettings.value(QStringLiteral("workspace/cleanVersion"),0).toInt()<1) {
        workspaceSettings.setValue(QStringLiteral("drnasDebug/fullscreen"),true);
        workspaceSettings.setValue(QStringLiteral("drnasDebug/immersive"),true);
        workspaceSettings.setValue(QStringLiteral("workspace/cleanVersion"),1);
        workspaceSettings.sync();
    }
#endif

    // Begin private, persistent startup logging before any FreeCAD dlopen.
    DrnasDiagnostics::start();
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
    // Live layout experiments are no longer part of the working workspace.
#endif
    DrnasDiagnostics::note(QStringLiteral("Qt GUI visible; entering event loop"));
    return app.exec();
}
