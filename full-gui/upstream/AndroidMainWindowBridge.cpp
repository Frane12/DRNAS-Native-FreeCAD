// SPDX-License-Identifier: LGPL-2.1-or-later
// DRNAS Android-only entrypoint compiled INTO the original upstream FreeCADGui.
// Does not fabricate a Qt/CAD shell: the window below is Gui::MainWindow.
// QtActivity has already created the only QApplication + event loop.
#include <App/Application.h>
#include <Base/Exception.h>
#include "Application.h"
#include "MainWindow.h"
#include "StartupProcess.h"
#include <QApplication>
#include <QCoreApplication>
#include <QThread>
#include <clocale>
#include <exception>
#include <memory>
#include <string>

namespace {
using StageCallback = void (*)(const char*);
std::string startupError;
std::unique_ptr<Gui::Application> originalGui;
std::unique_ptr<Gui::MainWindow> originalMainWindow;
// FreeCAD's argument parser may keep argv references; retain the buffer.
char executableName[] = "FreeCAD";
char* arguments[] = {executableName, nullptr};

void stage(StageCallback callback, const char* name)
{
    if (callback) {
        callback(name);
    }
}
} // namespace

extern "C" __attribute__((visibility("default")))
const char* drnas_freecad_android_last_error() noexcept
{
    return startupError.c_str();
}

// Never call Gui::Application::runApplication() from QtActivity: that would
// create a second QApplication and enter a second event loop. This bridge
// follows the original MainGui / Gui::Application boot sequence *inside*
// the already-running Android QApplication. Workbench modules and bundled
// Python resources still require their own Android integration and testing.
extern "C" __attribute__((visibility("default")))
int drnas_freecad_android_start(StageCallback callback) noexcept
{
    startupError.clear();
    if (!QCoreApplication::instance() || !qApp
        || QThread::currentThread() != qApp->thread()) {
        startupError = "Must launch on the existing Android QApplication UI thread";
        return -1;
    }
    if (originalMainWindow) {
        originalMainWindow->show();
        originalMainWindow->raise();
        originalMainWindow->activateWindow();
        return 0;
    }
    // A partially initialized singleton cannot safely be restarted in the
    // same process. The Qt shell will survive an exception, but a retry needs
    // a process restart. No silent fall-back to a fake FreeCAD MainWindow.
    static bool attempted = false;
    if (attempted) {
        startupError = "FreeCAD initialization was already attempted; restart app";
        return -2;
    }
    attempted = true;
    try {
        stage(callback, "Configuring genuine FreeCAD App runtime");
        auto& config = App::Application::Config();
        config["ExeName"] = "FreeCAD";
        config["ExeVendor"] = "FreeCAD";
        config["Application"] = "FreeCAD";
        config["AppDataSkipVendor"] = "true";
        config["RunMode"] = "Gui";
        config["Console"] = "0";
        config["LoggingConsole"] = "1";
        // Start with the built-in, upstream standard workbench. Additional
        // Part/Sketcher/PartDesign shared modules must be built and shipped.
        config["StartWorkbench"] = "NoneWorkbench";
        std::setlocale(LC_NUMERIC, "C");

        stage(callback, "App::Application::init (native FreeCAD + Python)");
        App::Application::init(1, arguments);

        stage(callback, "Gui::Application::initApplication");
        Gui::Application::initApplication();

        // QApplication exists; don't call StartupProcess::setupApplication(),
        // which sets attributes that must be chosen before Qt boots.
        stage(callback, "Gui::StartupProcess::execute");
        Gui::StartupProcess process;
        process.execute();

        stage(callback, "Gui::Application constructor");
        originalGui = std::make_unique<Gui::Application>(true);

        stage(callback, "Original Gui::MainWindow constructor");
        originalMainWindow = std::make_unique<Gui::MainWindow>();
        originalMainWindow->setProperty("QuitOnClosed", false);
        QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);

        stage(callback, "Original Gui::StartupPostProcess");
        Gui::StartupPostProcess post(originalMainWindow.get(), *originalGui, qApp);
        // QtActivity QApplication is not FreeCAD's GUISingleApplication;
        // disable the desktop IPC-only messageReceived connection.
        post.setLoadFromPythonModule(true);
        post.execute();

        stage(callback, "Showing original FreeCAD MainWindow");
        originalMainWindow->setProperty("eventLoop", true);
        originalMainWindow->show();
        originalMainWindow->raise();
        originalMainWindow->activateWindow();
        stage(callback, "Original FreeCAD MainWindow started");
        return 0;
    }
    catch (const Base::Exception& e) {
        startupError = e.what();
    }
    catch (const std::exception& e) {
        startupError = e.what();
    }
    catch (...) {
        startupError = "Unknown exception inside genuine FreeCAD initialization";
    }
    return -3;
}
