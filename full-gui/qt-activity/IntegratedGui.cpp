// The reference renderer is allocated lazily by Render Lab after Gui startup.
#include "NativeOcctViewport.h"
#include "DrnasRenderLab.h"
#include <algorithm>
#include <utility>
#include <QApplication>
#include "StartupDiagnostics.h"
#include "DrnasDebugCenter.h"
#include "DrnasTouchInput.h"
#include <QFont>
#include <QSizePolicy>
#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <cstdlib>
#include <QSettings>
#include <dlfcn.h>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPushButton>
#include <QPointer>
#include <QScreen>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QWindow>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>


// Copy the exact Android CPython 3.14 standard library supplied by the
// verified Python SDK into our private data directory before FreeCAD App::init.
// Qt's assets: filesystem is backed by Android's AssetManager; Python itself
// cannot import from that virtual filesystem.
namespace {

// Extract the original FreeCAD workbench resource tree into private filesystem.
// Android assets:/ is a Qt resource URL, not an importable Python directory.
bool configureBundledFreeCADHome(QString& error)
{
    const QString root = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + QStringLiteral("/drnas-freecad-home");
    QFile manifest(QStringLiteral("assets:/drnas/freecad-manifest.txt"));
    if (!manifest.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Bundled original FreeCAD GUI manifest missing");
        return false;
    }
    const QByteArray index = manifest.readAll();
    if (index.size() < 50 || !QDir().mkpath(root + QStringLiteral("/lib")) ||
        !QDir().mkpath(root + QStringLiteral("/bin"))) {
        error = QStringLiteral("Original FreeCAD resource manifest/home invalid");
        return false;
    }
    const QString stamp = root + QStringLiteral("/.asset-manifest");
    QFile previous(stamp);
    const bool current = previous.open(QIODevice::ReadOnly) &&
        previous.readAll() == index &&
        QFileInfo::exists(root + QStringLiteral("/Mod/Part/InitGui.py")) &&
        QFileInfo::exists(root + QStringLiteral("/Mod/Sketcher/InitGui.py")) &&
        QFileInfo::exists(root + QStringLiteral("/Mod/PartDesign/InitGui.py")) &&
        QFileInfo::exists(root + QStringLiteral("/data/Gui/Stylesheets/overlay/icons/taskshow.svg"));
    if (!current) {
        int count = 0;
        for (const QByteArray& record : index.split('\n')) {
            const QString relative = QString::fromUtf8(record.trimmed());
            if (relative.isEmpty()) continue;
            const QStringList segments = relative.split(QLatin1Char('/'));
            if (relative.startsWith(QLatin1Char('/')) ||
                relative.contains(QLatin1Char('\\')) ||
                segments.contains(QStringLiteral("..")) ||
                segments.contains(QStringLiteral(".")) ||
                segments.contains(QString())) {
                error = QStringLiteral("Unsafe original FreeCAD asset: %1").arg(relative);
                return false;
            }
            QFile input(QStringLiteral("assets:/drnas/freecad/") + relative);
            if (!input.open(QIODevice::ReadOnly)) {
                error = QStringLiteral("FreeCAD asset missing: %1").arg(relative);
                return false;
            }
            const QString destination = root + QLatin1Char('/') + relative;
            if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
                error = QStringLiteral("Cannot create FreeCAD resource folder: %1").arg(relative);
                return false;
            }
            QSaveFile output(destination);
            if (!output.open(QIODevice::WriteOnly)) {
                error = QStringLiteral("Cannot write FreeCAD asset: %1").arg(relative);
                return false;
            }
            while (!input.atEnd()) {
                const QByteArray bytes = input.read(256 * 1024);
                if (bytes.isEmpty() || output.write(bytes) != bytes.size()) {
                    output.cancelWriting();
                    error = QStringLiteral("Failed to copy FreeCAD asset: %1").arg(relative);
                    return false;
                }
            }
            if (!output.commit()) {
                error = QStringLiteral("Failed to commit FreeCAD asset: %1").arg(relative);
                return false;
            }
            ++count;
        }
        if (count < 25) {
            error = QStringLiteral("Too few original FreeCAD GUI assets");
            return false;
        }
        QSaveFile saved(stamp);
        if (!saved.open(QIODevice::WriteOnly) ||
            saved.write(index) != index.size() || !saved.commit()) {
            error = QStringLiteral("Could not save FreeCAD GUI asset manifest");
            return false;
        }
    }
    // FreeCAD's original overlay/stylesheet icons are referenced using
    // qss:overlay/icons/*.svg, not absolute paths. Desktop FreeCAD registers
    // this Qt search-path prefix during style setup, but Android's custom
    // QtActivity startup does not. The SVGs ARE packaged and extracted above;
    // connect FreeCAD's qss: URLs to that verified on-device asset directory.
    const QString stylesheets = root + QStringLiteral("/data/Gui/Stylesheets");
    // FreeCAD's Qt stylesheet loader may reset the qss search prefix during
    // MainWindow startup. Register both the extracted filesystem root and the
    // Android resource alias, and restore the prefix after upstream startup.
    QDir::setSearchPaths(QStringLiteral("qss"), QStringList{stylesheets});
    for (const char* icon : {
             "taskshow.svg", "taskshow_lightgray.svg",
             "editshow.svg", "editshow_lightgray.svg",
             "edithide.svg", "edithide_lightgray.svg",
             "float.svg", "overlay.svg", "close.svg", "mode.svg",
             "transparent.svg", "autohide.svg", "autohide_lightgray.svg"}) {
        const QString uri = QStringLiteral("qss:overlay/icons/") + QString::fromLatin1(icon);
        if (!QFileInfo::exists(uri)) {
            error = QStringLiteral("FreeCAD QSS icon path unresolved: %1 (source %2)")
                        .arg(uri, stylesheets);
            return false;
        }
    }
    // Use existing Android app-private directories before FreeCAD initializes
    // std::filesystem paths. New creates a transient directory in this cache.
    const QString userRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/freecad-user");
    const QString tempRoot = userRoot + QStringLiteral("/temp");
    for (const QString& directory : {userRoot, tempRoot}) {
        if (!QDir().mkpath(directory)) {
            error = QStringLiteral("Cannot create FreeCAD private directory: ") + directory;
            return false;
        }
    }
    for (const auto& entry : {std::pair<const char*, QString>{"FREECAD_USER_HOME", userRoot},
                             {"FREECAD_USER_DATA", userRoot},
                             {"FREECAD_USER_TEMP", tempRoot}, {"TMPDIR", tempRoot}}) {
        if (!qputenv(entry.first, entry.second.toUtf8())) {
            error = QStringLiteral("Cannot configure FreeCAD private paths");
            return false;
        }
        DrnasDiagnostics::note(QStringLiteral("STORAGE CONFIG %1=%2")
            .arg(QString::fromLatin1(entry.first), entry.second));
    }
    const QByteArray home = (root + QLatin1Char('/')).toUtf8();
    if (setenv("DRNAS_FREECAD_HOME", home.constData(), 1) != 0) {
        error = QStringLiteral("Failed to set original FreeCAD home path");
        return false;
    }
    return true;
}

bool configureBundledPython(QString& error)
{
    const QString root = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + QStringLiteral("/drnas-python314");
    if (!QDir().mkpath(root)) {
        error = QStringLiteral("Cannot create private Python runtime directory: %1").arg(root);
        return false;
    }
    QFile source(QStringLiteral("assets:/drnas/python314.zip"));
    if (!source.open(QIODevice::ReadOnly) || source.size() < 10000) {
        error = QStringLiteral("Missing embedded Python 3.14 standard library asset: %1")
                    .arg(source.errorString());
        return false;
    }
    const QString archive = root + QStringLiteral("/python314.zip");
    if (!QFileInfo::exists(archive) || QFileInfo(archive).size() != source.size()) {
        QSaveFile target(archive);
        if (!target.open(QIODevice::WriteOnly)) {
            error = QStringLiteral("Cannot stage Python standard library: %1")
                        .arg(target.errorString());
            return false;
        }
        while (!source.atEnd()) {
            const QByteArray chunk = source.read(256 * 1024);
            if (chunk.isEmpty() || target.write(chunk) != chunk.size()) {
                error = QStringLiteral("Writing Python standard library failed");
                target.cancelWriting();
                return false;
            }
        }
        if (!target.commit()) {
            error = QStringLiteral("Committing Python standard library failed: %1")
                        .arg(target.errorString());
            return false;
        }
    }
    // Standard-library extension modules (for example _struct and _ssl) are
    // compiled for Android and cannot be imported directly out of a zipfile.
    // CPython's official Android testbed likewise extracts these assets.
    QFile manifest(QStringLiteral("assets:/drnas/python-ext-manifest.txt"));
    if (!manifest.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Python native-extension manifest missing: %1")
                    .arg(manifest.errorString());
        return false;
    }
    const QString extdir = root + QStringLiteral("/lib-dynload");
    if (!QDir().mkpath(extdir)) {
        error = QStringLiteral("Cannot create Python extension directory");
        return false;
    }
    const QList<QByteArray> extensions = manifest.readAll().split('\n');
    int extensionCount = 0;
    for (const QByteArray& nameBytes : extensions) {
        const QString name = QString::fromUtf8(nameBytes.trimmed());
        if (name.isEmpty()) continue;
        // Never allow a manifest entry to escape app-private storage.
        if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
            || name.contains(QStringLiteral("..")) || !name.endsWith(QStringLiteral(".so"))) {
            error = QStringLiteral("Unsafe embedded Python extension filename: %1").arg(name);
            return false;
        }
        QFile module(QStringLiteral("assets:/drnas/python-ext/") + name);
        if (!module.open(QIODevice::ReadOnly) || module.size() == 0) {
            error = QStringLiteral("Missing Android CPython module: %1").arg(name);
            return false;
        }
        const QString dest = extdir + QLatin1Char('/') + name;
        if (!QFileInfo::exists(dest) || QFileInfo(dest).size() != module.size()) {
            QSaveFile target(dest);
            if (!target.open(QIODevice::WriteOnly)) {
                error = QStringLiteral("Cannot stage Python extension: %1").arg(name);
                return false;
            }
            while (!module.atEnd()) {
                const QByteArray bytes = module.read(256 * 1024);
                if (bytes.isEmpty() || target.write(bytes) != bytes.size()) {
                    target.cancelWriting();
                    error = QStringLiteral("Cannot copy Python extension: %1").arg(name);
                    return false;
                }
            }
            if (!target.commit()) {
                error = QStringLiteral("Cannot commit Python extension: %1").arg(name);
                return false;
            }
        }
        ++extensionCount;
    }
    if (!extensionCount) {
        error = QStringLiteral("No Android Python extension modules were bundled");
        return false;
    }
    // libpython has already been loaded by the explicit FreeCAD dlopen test.
    dlerror();
    void* pyInit = dlsym(RTLD_DEFAULT, "Py_InitializeFromConfig");
    Dl_info location{};
    if (!pyInit || !dladdr(pyInit, &location) || !location.dli_fname) {
        error = QStringLiteral("libpython3.14.so location unavailable");
        return false;
    }
    const QString nativeDir = QFileInfo(QString::fromLocal8Bit(location.dli_fname))
                                  .absolutePath();
    const QByteArray runtimeBytes = root.toUtf8();
    const QByteArray nativeBytes = nativeDir.toUtf8();
    if (setenv("DRNAS_PYTHON_RUNTIME", runtimeBytes.constData(), 1) != 0 ||
        setenv("DRNAS_PYTHON_NATIVE_DIR", nativeBytes.constData(), 1) != 0 ||
        setenv("PATH_TO_FREECAD_LIBDIR", nativeBytes.constData(), 1) != 0) {
        error = QStringLiteral("Could not configure embedded Python environment");
        return false;
    }
    return true;
}
}

// A restart-safe breadcrumb for *each stage* of real upstream MainWindow
// startup. Native aborts (OpenGL/GL4ES/Python) cannot be caught in C++.
namespace {
QLabel* currentFreeCADStartupStatus = nullptr;
QMainWindow* currentFreeCADAndroidShell = nullptr;
bool originalFreeCADEmbeddedBeforeShow = false;

// FreeCAD's desktop startup can change the qss: prefix while it creates
// OverlayManager / ReportView. Reassert the Android-extracted assets before
// EVERY stage (particularly before MainWindow and StartupPostProcess), not
// just after it is already too late for icon initialization.
void restoreOriginalFreeCADIconPaths()
{
    const QString iconRoot = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation)
        + QStringLiteral("/drnas-freecad-home/data/Gui/Stylesheets");
    if (!QFileInfo(iconRoot).isDir()) {
        return;
    }
    QStringList paths = QDir::searchPaths(QStringLiteral("qss"));
    paths.removeAll(iconRoot);
    paths.prepend(iconRoot);
    QDir::setSearchPaths(QStringLiteral("qss"), paths);
}

// Qt for Android maps floating QDockWidgets to independent native platform
// windows. FreeCAD's desktop layout restoration can float "Report view", and
// Android then dispatches touches to already-destroyed window IDs.
// Normalize *all* desktop docks to child widgets, before first show and after
// the upstream layout/overlay restoration. Do not resize the whole desktop.
void normalizeAndroidFreeCADDocks(QMainWindow* window)
{
    if (!window) return;
    for (QDockWidget* dock : window->findChildren<QDockWidget*>()) {
        if (!dock) continue;
        const bool report = dock->objectName() == QStringLiteral("Report view")
            || dock->windowTitle() == QStringLiteral("Report View")
            || dock->windowTitle() == QStringLiteral("Pregled izvješća");

        // Disable dragging a dock into a separate Android QWindow. A dock
        // remains available through FreeCAD's View > Panels menu.
        dock->setFeatures(dock->features() & ~QDockWidget::DockWidgetFloatable);
        if (dock->isFloating()) {
            const bool shouldShow = dock->isVisible() && !report;
            dock->hide();
            if (window->dockWidgetArea(dock) == Qt::NoDockWidgetArea) {
                window->addDockWidget(report ? Qt::BottomDockWidgetArea
                                              : Qt::LeftDockWidgetArea, dock);
            }
            dock->setFloating(false);
            if (shouldShow) dock->show();
        }
        if (report) {
            // Report view is an optional debug panel and must not obstruct
            // the actual FreeCAD 3D workspace at phone dimensions.
            dock->hide();
        }
        if (!dock->property("drnasAndroidDockGuard").toBool()) {
            dock->setProperty("drnasAndroidDockGuard", true);
            QPointer<QMainWindow> safeWindow(window);
            QObject::connect(dock, &QDockWidget::topLevelChanged, window,
                             [safeWindow](bool floating) {
                if (!floating || !safeWindow) return;
                // Window-state restore may run AFTER GUI startup. Defer until
                // Qt finishes the transition, then remove floating windows.
                QTimer::singleShot(0, safeWindow.data(), [safeWindow] {
                    if (safeWindow) normalizeAndroidFreeCADDocks(safeWindow.data());
                });
            });
        }
    }
}

// An embedded FreeCAD menu bar uses a second row on small Android screens.
// Keep the original menus and real workbench toolbars alive, but expose menus
// through the touch-friendly DRNAS dropdown instead of duplicating the row.
// Never forcibly show workbench toolbars: NoneWorkbench legitimately has none.
void compactOriginalAndroidFreeCADChrome(QMainWindow* window)
{
    if (!window) return;
    if (QMenuBar* menu = window->menuBar()) {
        menu->setNativeMenuBar(false);
        menu->hide();
    }
    for (QToolBar* bar : window->findChildren<QToolBar*>()) {
        if (bar->iconSize()!=QSize(24, 24)) bar->setIconSize(QSize(24, 24));
        bar->setMovable(false);
        if (bar->isFloating()) window->addToolBar(Qt::TopToolBarArea, bar);
        bar->setFloatable(false);
    }
    normalizeAndroidFreeCADDocks(window);
}

// The stock FreeCAD startup bridge intentionally requests NoneWorkbench.
// Upstream NoneWorkbench contains no CAD toolbar and only Quit/Preferences/
// Workbench/Help menu entries. On Android the embedded desktop menu bar may be
// hidden by the QtActivity/native window policy, leaving a completely grey UI.
// Expose REAL upstream commands through a plain QWidget toolbar hosted by the
// one stable QtActivity QMainWindow. This does NOT replace FreeCAD's viewport.
bool runOriginalFreeCADPython(const QString& script, QString& error)
{
    using IsInitialized = int (*)();
    using AcquireGIL = int (*)();
    using ReleaseGIL = void (*)(int);
    using RunPython = int (*)(const char*, void*);
    auto isInitialized = reinterpret_cast<IsInitialized>(
        dlsym(RTLD_DEFAULT, "Py_IsInitialized"));
    auto acquire = reinterpret_cast<AcquireGIL>(
        dlsym(RTLD_DEFAULT, "PyGILState_Ensure"));
    auto release = reinterpret_cast<ReleaseGIL>(
        dlsym(RTLD_DEFAULT, "PyGILState_Release"));
    auto execute = reinterpret_cast<RunPython>(
        dlsym(RTLD_DEFAULT, "PyRun_SimpleStringFlags"));
    if (!isInitialized || !acquire || !release || !execute || !isInitialized()) {
        error = QStringLiteral("Embedded FreeCAD Python runtime unavailable");
        return false;
    }
    DrnasDiagnostics::note(QStringLiteral("FreeCAD Python command: ") + script.left(500));
    const QByteArray command = script.toUtf8();
    DrnasDiagnostics::note(QStringLiteral("PYTHON checkpoint: acquire GIL"));
    const int state = acquire();
    // Persist boundaries before crossing into embedded Python; a native abort
    // cannot be caught by C++ and must leave a useful previous-launch trace.
    DrnasDiagnostics::note(QStringLiteral("PYTHON checkpoint: execute begin"));
    const auto isWorkbenchActivation = script.contains(QStringLiteral("activateWorkbench("));
    if (isWorkbenchActivation) {
        DrnasDiagnostics::note(QStringLiteral("WB CRITICAL: PyRun_SimpleStringFlags ENTER; command=") + script.left(300));
        DrnasDiagnostics::note(QStringLiteral("WB CRITICAL: if next checkpoint is absent, failure is inside Python/FreeCAD workbench activation"));
    }
    const int result = execute(command.constData(), nullptr);
    if (isWorkbenchActivation)
        DrnasDiagnostics::note(QStringLiteral("WB CRITICAL: PyRun_SimpleStringFlags RETURN code=%1").arg(result));
    DrnasDiagnostics::note(QStringLiteral("PYTHON checkpoint: execute returned=%1").arg(result));
    release(state);
    DrnasDiagnostics::note(QStringLiteral("PYTHON checkpoint: GIL released"));
    if (result != 0) {
        error = QStringLiteral("Original FreeCAD Python command failed (see Android log)");
        DrnasDiagnostics::note(error);
        return false;
    }
    return true;
}

// Child overlay: never docks into the CAD layout or creates an Android window.
class DrnasFloatingCommands : public QObject {
    QMainWindow* host;
    QWidget* panel;
    QPushButton* launcher;
    QWidget* grip;
    QPoint dragOffset;
    bool dragging = false;
    void constrain() {
        const int w = std::max(1, std::min(460, host->width() - 12));
        panel->setFixedWidth(w);
        panel->adjustSize();
        panel->move(std::clamp(panel->x(), 0, std::max(0, host->width()-panel->width())),
                    std::clamp(panel->y(), 0, std::max(0, host->height()-panel->height())));
        launcher->setGeometry(std::max(0, host->width()-92),
                              std::max(0, host->height()-84), 80, 44);
        panel->raise(); launcher->raise();
    }
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (obj == host && event->type() == QEvent::Resize) constrain();
        if (obj == grip) {
            auto* mouse = dynamic_cast<QMouseEvent*>(event);
            if (mouse && event->type() == QEvent::MouseButtonPress &&
                mouse->button() == Qt::LeftButton) {
                dragging = true;
                dragOffset = mouse->globalPosition().toPoint()-panel->mapToGlobal(QPoint());
                return true;
            }
            if (mouse && event->type() == QEvent::MouseMove && dragging) {
                panel->move(host->mapFromGlobal(mouse->globalPosition().toPoint()-dragOffset));
                constrain(); return true;
            }
            if (event->type() == QEvent::MouseButtonRelease && dragging) {
                dragging = false; return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
public:
    DrnasFloatingCommands(QMainWindow* shell, QToolBar* toolbar)
        : QObject(shell), host(shell) {
        panel = new QWidget(host);
        panel->setObjectName(QStringLiteral("drnasFloatingCommands"));
        panel->setStyleSheet(QStringLiteral("background:#182d42;color:white;"));
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(3,3,3,3); layout->setSpacing(2);
        auto* row = new QHBoxLayout;
        auto* title = new QLabel(QStringLiteral("DRNAS tools — drag"), panel);
        title->setMinimumHeight(30);
        grip = title; grip->installEventFilter(this);
        auto* close = new QPushButton(QStringLiteral("×"), panel);
        close->setFixedSize(34,30);
        row->addWidget(title,1); row->addWidget(close);
        layout->addLayout(row);
        toolbar->setParent(panel);
        toolbar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        layout->addWidget(toolbar);
        launcher = new QPushButton(QStringLiteral("Tools"), host);
        launcher->setObjectName(QStringLiteral("drnasFloatingCommandsLauncher"));
        launcher->setStyleSheet(QStringLiteral("background:#315775;color:white;border-radius:5px;"));
        connect(launcher, &QPushButton::clicked, this, [this] {
            panel->setVisible(!panel->isVisible()); constrain();
        });
        connect(close, &QPushButton::clicked, panel, &QWidget::hide);
        host->installEventFilter(this);
        panel->move(6,6); constrain();
        panel->hide(); launcher->show();
        DrnasDiagnostics::note(QStringLiteral("DRNAS floating tools: collapsed; CAD layout unreserved"));
    }
};


void recordNativeGlesViewport(QMainWindow* freecad)
{
    // Earlier GPU03 readings also went through GL4ES. Query the Android GLES
    // dispatch library explicitly, using read-only queries in the viewport context.
    QOpenGLWidget* widget = nullptr;
    if (freecad) {
        for (auto* candidate : freecad->findChildren<QOpenGLWidget*>()) {
            if (candidate->isVisible() && candidate->isValid() &&
                candidate->width() > 100 && candidate->height() > 100) {
                widget = candidate;
                break;
            }
        }
    }
    if (!widget) {
        DrnasDiagnostics::note(QStringLiteral("GPU_NATIVE: no visible document viewport; create a document first"));
        return;
    }
    // A valid widget can outlive its Android EGLSurface during rotation or
    // the first raster-to-OpenGL window transition. Diagnostics must not
    // force makeCurrent() against an unexposed/destroyed native window.
    QWindow* nativeWindow = widget->window()->windowHandle();
    if (!nativeWindow || !nativeWindow->isExposed() ||
        !widget->updatesEnabled() ||
        QGuiApplication::applicationState() != Qt::ApplicationActive) {
        DrnasDiagnostics::note(QStringLiteral("GPU_NATIVE deferred: native surface not ready"));
        return;
    }
    QOpenGLContext* previous = QOpenGLContext::currentContext();
    QSurface* previousSurface = previous ? previous->surface() : nullptr;
    widget->makeCurrent();
    void* gles = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    using ReadString = const unsigned char* (*)(unsigned int);
    using ReadInteger = void (*)(unsigned int, int*);
    auto readString = gles ? reinterpret_cast<ReadString>(dlsym(gles, "glGetString")) : nullptr;
    auto readInteger = gles ? reinterpret_cast<ReadInteger>(dlsym(gles, "glGetIntegerv")) : nullptr;
    const bool current = QOpenGLContext::currentContext() == widget->context();
    if (current && readString && readInteger) {
        Dl_info provider = {};
        dladdr(reinterpret_cast<const void*>(readString), &provider);
        DrnasDiagnostics::note(QStringLiteral("GPU_NATIVE glGetString provider=%1")
            .arg(QString::fromLocal8Bit(provider.dli_fname ? provider.dli_fname : "(unknown)")));
        const unsigned int tokens[] = {0x1F00, 0x1F01, 0x1F02, 0x8B8C};
        const char* names[] = {"VENDOR", "RENDERER", "VERSION", "GLSL"};
        for (int i = 0; i < 4; ++i) {
            const unsigned char* value = readString(tokens[i]);
            DrnasDiagnostics::note(QStringLiteral("GPU_NATIVE %1=%2")
                .arg(QString::fromLatin1(names[i]),
                     value ? QString::fromLatin1(reinterpret_cast<const char*>(value)) : QStringLiteral("(null)")));
        }
        int fbo = -1, viewport[4] = {}, depth = -1, stencil = -1;
        readInteger(0x8CA6, &fbo);       // GL_FRAMEBUFFER_BINDING
        readInteger(0x0BA2, viewport);   // GL_VIEWPORT
        readInteger(0x0D56, &depth);     // GL_DEPTH_BITS
        readInteger(0x0D57, &stencil);   // GL_STENCIL_BITS
        DrnasDiagnostics::note(QStringLiteral(
            "GPU_NATIVE FBO=%1 QtFBO=%2 depth=%3 stencil=%4 viewport=%5,%6,%7,%8 widgetLogical=%9x%10 dpr=%11")
            .arg(fbo).arg(widget->defaultFramebufferObject()).arg(depth).arg(stencil)
            .arg(viewport[0]).arg(viewport[1]).arg(viewport[2]).arg(viewport[3])
            .arg(widget->width()).arg(widget->height()).arg(widget->devicePixelRatioF()));
    } else {
        DrnasDiagnostics::note(QStringLiteral("GPU_NATIVE unavailable: current=%1 GLES=%2 strings=%3 integers=%4")
            .arg(current).arg(gles != nullptr).arg(readString != nullptr).arg(readInteger != nullptr));
    }
    if (gles) dlclose(gles);
    // Restore the caller's current-context choice. No extra GL surface is created.
    if (previous != widget->context()) {
        widget->doneCurrent();
        if (previous && previousSurface) previous->makeCurrent(previousSurface);
    }
}

void installOriginalFreeCADTouchControls(QMainWindow* shell, QMainWindow* freecad)
{
    if (!shell || !freecad || shell->findChild<QToolBar*>(
            QStringLiteral("drnasAndroidFreeCADCommands"))) return;

    QPointer<QMainWindow> upstream(freecad);
    auto* toolbar = new QToolBar(QStringLiteral("FreeCAD Android commands"), shell);
    toolbar->setObjectName(QStringLiteral("drnasAndroidFreeCADCommands"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setAllowedAreas(Qt::NoToolBarArea);
    toolbar->setProperty("drnasFloatingOverlay", true);
    toolbar->setIconSize(QSize(28, 28));
    // Qt Android defaults QToolBar buttons to icon-only. These are explicit
    // text commands with no icons, so force their captions to render.
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setStyleSheet(QStringLiteral(
        "QToolBar{background:#182d42;border:0px;spacing:3px;padding:2px;}"
        "QToolButton{color:white;background:#315775;border:1px solid #52718d;"
        "border-radius:4px;min-height:30px;padding:3px 6px;font-size:12px;}"
        "QToolButton:pressed{background:#4383a9;}"
        "QLabel{color:#eaf2f8;font-size:11px;padding:2px;}"));
    // Mounted as a child overlay after actions are populated.

    auto* resultLabel = new QLabel(QStringLiteral("Choose Part to begin"), toolbar);
    resultLabel->setObjectName(QStringLiteral("drnasAndroidCommandResult"));
    resultLabel->setMinimumWidth(75);
    auto report = [resultLabel, upstream](const QString& message) {
        resultLabel->setText(message);
        resultLabel->setToolTip(message);
        if (upstream && upstream->statusBar())
            upstream->statusBar()->showMessage(message, 7000);
        QSettings settings;
        settings.setValue(QStringLiteral("nativeProbe/lastTouchCommand"), message);
        settings.sync();
        qInfo().noquote() << "DRNAS FREECAD ANDROID:" << message;
    };
    // These are actual embedded FreeCAD/FreeCADGui Python entrypoints.
    // No mock modeling, and no second desktop QApplication or native window.
    auto invoke = [report, upstream](const QString& code, const QString& label) {
        QString error;
        if (label == QStringLiteral("New document"))
            DrnasDiagnostics::note(QStringLiteral("NEW_CPP C02 before runOriginalFreeCADPython"));
        const bool pythonOk = runOriginalFreeCADPython(code, error);
        if (label == QStringLiteral("New document"))
            DrnasDiagnostics::note(QStringLiteral("NEW_CPP C02R Python bridge returned result=%1").arg(pythonOk));
        if (pythonOk) {
            report(label + QStringLiteral(" OK"));
            // A workbench switch rebuilds the original menus and toolbars.
            // Recompact them only after the upstream command finishes.
            if (upstream) {
                compactOriginalAndroidFreeCADChrome(upstream.data());
                QTimer::singleShot(0, upstream.data(), [upstream] {
                    if (upstream) compactOriginalAndroidFreeCADChrome(upstream.data());
                });
            }
        } else {
            report(label + QStringLiteral(": ") + error);
        }
    };
    auto* newDoc = toolbar->addAction(QStringLiteral("NEW +"));
    newDoc->setToolTip(QStringLiteral("FreeCAD.newDocument()"));
    QObject::connect(newDoc, &QAction::triggered, shell, [invoke] {
        DrnasDiagnostics::note(QStringLiteral("NEW_CPP C00 QAction triggered: entering handler"));
        DrnasDiagnostics::note(QStringLiteral("TOUCH New document: App document + GUI observer boundary"));
        // Persist checkpoints directly to the same previous-launch trace used by
        // workbench probes. A SIGSEGV/abort inside newDocument cannot be caught
        // by Python, but the last durable marker identifies the failing phase.
        DrnasDiagnostics::note(QStringLiteral("NEW_CPP C01 before invoke/Python bridge"));
        invoke(QStringLiteral(R"DRNAS(
import os, sys, time, traceback, faulthandler
_drnas_path = os.environ.get('DRNAS_WB_LOG_PATH', '')
def _drnas_new_mark(msg):
    if _drnas_path:
        fd = os.open(_drnas_path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        try:
            os.write(fd, ('[NEW_DOC] ' + msg + '\n').encode('utf-8', 'replace'))
            os.fsync(fd)
        finally:
            os.close(fd)
# Keep a dedicated fd alive so native fatal signals can leave a Python stack.
_drnas_fault_fd = None
try:
    _drnas_new_mark('N00 ENTER NEW bridge pid=' + str(os.getpid()))
    if _drnas_path:
        _drnas_fault_fd = os.open(_drnas_path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        faulthandler.enable(file=_drnas_fault_fd, all_threads=True)
    _drnas_new_mark('N01 before import FreeCAD')
    import FreeCAD as App
    _drnas_new_mark('N02 FreeCAD imported')
    _drnas_new_mark('N03 before listDocuments')
    _drnas_before = tuple(App.listDocuments().keys())
    _drnas_new_mark('N04 listDocuments OK count=' + str(len(_drnas_before)))
    _drnas_new_mark('N05 before import FreeCADGui')
    import FreeCADGui as Gui
    _drnas_new_mark('N06 FreeCADGui imported')
    _drnas_new_mark('N07 before App.newDocument; GUI observers may run synchronously')
    _drnas_t0 = time.monotonic_ns()
    _drnas_doc = App.newDocument()
    _drnas_new_mark('N08 App.newDocument RETURN elapsed_ms=' + str((time.monotonic_ns()-_drnas_t0)//1000000))
    _drnas_new_mark('N09 before document.Name')
    _drnas_new_mark('N10 document.Name=' + str(_drnas_doc.Name))
    _drnas_new_mark('N11 before Gui.activeDocument')
    _drnas_gui_doc = Gui.activeDocument()
    _drnas_new_mark('N12 Gui.activeDocument present=' + str(_drnas_gui_doc is not None))
    _drnas_new_mark('N13 NEW completed')
except BaseException:
    _drnas_new_mark('NERR PYTHON EXCEPTION ' + traceback.format_exc())
    raise
finally:
    # Restore the process-wide fault journal before closing the scoped fd.
    if _drnas_fault_fd is not None:
        faulthandler.enable(file=_drnas_fault_file, all_threads=True)
        os.close(_drnas_fault_fd)
)DRNAS"),
               QStringLiteral("New document"));
        DrnasDiagnostics::note(QStringLiteral("NEW_CPP C03 Python invocation returned to QAction handler"));
    });
    // Session-only opt-in. A crash restarts with the blank viewport fallback.
    auto* coinRender = toolbar->addAction(QStringLiteral("TEST Coin render"));
    coinRender->setObjectName(QStringLiteral("drnasTestCoinRender"));
    coinRender->setCheckable(true);
    coinRender->setChecked(qgetenv("DRNAS_ENABLE_COIN_REDRAW") == QByteArray("1"));
    coinRender->setToolTip(QStringLiteral("Enable genuine Coin rendering with GPU/context diagnostics; may crash. Copy Log after restart."));
    QObject::connect(coinRender, &QAction::triggered, shell,
                     [invoke, report, upstream](bool enabled) {
        qputenv("DRNAS_ENABLE_COIN_REDRAW", enabled ? QByteArray("1") : QByteArray("0"));
        DrnasDiagnostics::note(QStringLiteral("RENDER PROBE: genuine Coin redraw enabled=%1; session only").arg(enabled));
        report(enabled ? QStringLiteral("Coin render test enabled; create a document or redraw")
                       : QStringLiteral("Coin render test disabled; blank viewport fallback"));
        if (enabled) {
            recordNativeGlesViewport(upstream.data());
            invoke(QStringLiteral(
                "import FreeCADGui as Gui\n"
                "if Gui.activeDocument() is not None:\n"
                "    Gui.activeDocument().activeView().redraw()\n"),
                QStringLiteral("Coin redraw probe"));
        }
    });
    coinRender->setVisible(false); // Replaced by explicit Render Lab phases.
    auto* renderLab = new DrnasRenderLab(shell, freecad, toolbar,
        [](const QString& code, QString& error) { return runOriginalFreeCADPython(code,error); });
    // Choose before New in a fresh launch. Never compare A/B by switching
    // already-mutated Coin/GL state within an existing document.
    const QString initialAb = qgetenv("DRNAS_VIEWPORT_AB") == QByteArray("B")
        ? QStringLiteral("B") : QStringLiteral("A");
    auto* abStatus = new QLabel(QStringLiteral("A/B: ") + initialAb, toolbar);
    const bool abAvailable = dlsym(RTLD_DEFAULT, "drnas_viewport_ab_version") != nullptr;
    DrnasDiagnostics::note(QStringLiteral("VIEWPORT_AB STARTUP mode=%1 nativeAvailable=%2; default Coin3D stage 3")
        .arg(initialAb).arg(abAvailable));
    for (const char mode : {'A', 'B'}) {
        auto* action = toolbar->addAction(QStringLiteral("TEST %1").arg(QChar(mode)));
        action->setObjectName(QStringLiteral("drnasViewportTest%1").arg(QChar(mode)));
        action->setEnabled(abAvailable);
        action->setToolTip(mode == 'A'
            ? QStringLiteral("A: original Coin viewport. Select before New after restarting app.")
            : QStringLiteral("B: sync Coin to actual GL viewport. Select before New after restarting app."));
        QObject::connect(action, &QAction::triggered, shell,
            [upstream, report, coinRender, abStatus, mode, renderLab] {
                if (!upstream) return;
                for (auto* area : upstream->findChildren<QMdiArea*>()) {
                    if (!area->subWindowList().isEmpty()) {
                        report(QStringLiteral("A/B: restart app and choose TEST A or TEST B before New"));
                        return;
                    }
                }
                qputenv("DRNAS_ENABLE_COIN_REDRAW", QByteArray("0"));
                coinRender->setChecked(false);
                renderLab->setStage(0);
                qputenv("DRNAS_VIEWPORT_AB", QByteArray(1, mode));
                abStatus->setText(QStringLiteral("A/B: %1").arg(QChar(mode)));
                DrnasDiagnostics::note(QStringLiteral("VIEWPORT_AB SELECT mode=%1; fresh document required; render remains off")
                    .arg(QChar(mode)));
                report(QStringLiteral("TEST %1 selected. New, then TEST Coin render; keep the same startup scale and rotation sequence.")
                    .arg(QChar(mode)));
            });
    }
    toolbar->addWidget(abStatus);
    if (!abAvailable) {
        abStatus->setText(QStringLiteral("A/B: awaiting native build"));
        DrnasDiagnostics::note(QStringLiteral("VIEWPORT_AB unavailable in bundled native library; test buttons disabled"));
    }
    auto* storageTest = toolbar->addAction(QStringLiteral("Storage"));
    QObject::connect(storageTest, &QAction::triggered, shell, [invoke] {
        invoke(QStringLiteral("_drnas_storage_test()"), QStringLiteral("Storage probe"));
    });
    auto* coreTest = toolbar->addAction(QStringLiteral("New no view"));
    QObject::connect(coreTest, &QAction::triggered, shell, [invoke] {
        invoke(QStringLiteral(
            "import FreeCAD as App\n"
            "_drnas_storage_test()\n"
            "_drnas_mark('CORE NEW BEGIN hidden=True')\n"
            "_drnas_core_doc = App.newDocument('DRNAS_CoreProbe', hidden=True)\n"
            "_drnas_mark('CORE NEW RETURN transient=' + repr(_drnas_core_doc.TransientDir))\n"
            "App.closeDocument(_drnas_core_doc.Name)\n"
            "_drnas_mark('CORE NEW CLOSED')\n"), QStringLiteral("Core document probe"));
    });
    // DRNAS crash isolation: opt-in tests in the SAME original GUI, without
    // automatic workbench activation or an additional Qt/OpenGL window.
    // The existing Android Python bridge acquires/releases the GIL.
    for (const auto& probe : {
             std::pair<const char*, const char*>{"TEST 1 Part", "import Part"},
             {"TEST 2 PartGui", "import PartGui"},
             {"TEST 3 Activate", R"DRNAS(
import os, sys, faulthandler, traceback, FreeCADGui as Gui
_drnas_path = os.environ.get('DRNAS_WB_LOG_PATH', '')
_drnas_fd = os.open(_drnas_path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600) if _drnas_path else -1
_drnas_count = 0
def _drnas_mark(message):
    if _drnas_fd >= 0:
        os.write(_drnas_fd, ('[PYTRACE] ' + message + '\n').encode('utf-8', 'replace'))
def _drnas_trace(frame, event, arg):
    global _drnas_count
    if event not in ('call', 'line', 'exception') or _drnas_count >= 1200:
        return _drnas_trace
    path = frame.f_code.co_filename.replace('\\', '/')
    if '/Mod/Part/' in path or path.endswith('/Part/InitGui.py') or '/Mod/PartDesign/' in path:
        _drnas_count += 1
        _drnas_mark('%04d %s %s:%d %s' % (_drnas_count, event, path, frame.f_lineno, frame.f_code.co_name))
    return _drnas_trace
_drnas_mark('TEST 3: before activateWorkbench(PartWorkbench)')
sys.settrace(_drnas_trace)
try:
    Gui.activateWorkbench('PartWorkbench')
    _drnas_mark('TEST 3: activateWorkbench returned OK')
except BaseException:
    sys.settrace(None)
    _drnas_mark('TEST 3: PYTHON EXCEPTION BEGIN')
    for _drnas_line in traceback.format_exc().splitlines():
        _drnas_mark(_drnas_line)
    _drnas_mark('TEST 3: PYTHON EXCEPTION END')
finally:
    sys.settrace(None)
    _drnas_mark('TEST 3: trace disabled')
    if _drnas_fd >= 0:
        os.close(_drnas_fd)
)DRNAS"}}) {
        const QString caption = QString::fromLatin1(probe.first);
        const QString command = QString::fromLatin1(probe.second);
        QAction* test = toolbar->addAction(caption);
        test->setToolTip(QStringLiteral("Isolated Part crash checkpoint; inspect DRNAS log if app exits"));
        QObject::connect(test, &QAction::triggered, shell,
                         [invoke, caption, command, report] {
            QSettings settings;
            const QString pending = QStringLiteral("nativeProbe/partStagePending");
            settings.setValue(pending, caption);
            settings.sync();
            DrnasDiagnostics::note(QStringLiteral("PART PROBE BEGIN: ") + caption);
            QString error;
            const bool ok = runOriginalFreeCADPython(command, error);
            DrnasDiagnostics::note(QStringLiteral("PART PROBE RETURN: %1 result=%2 error=%3")
                .arg(caption).arg(ok).arg(error));
            settings.remove(pending);
            settings.sync();
            report(caption + (ok ? QStringLiteral(" PASS")
                                  : QStringLiteral(" FAIL: ") + error));
        });
    }
    toolbar->addSeparator();
    toolbar->addSeparator();
    for (const auto& workbench : {
             std::pair<const char*, const char*>{"Part", "PartWorkbench"},
             {"Sketcher", "SketcherWorkbench"},
             {"PartDesign", "PartDesignWorkbench"}}) {
        const QString label = QString::fromLatin1(workbench.first);
        const QString name = QString::fromLatin1(workbench.second);
        QAction* action = toolbar->addAction(label);
        QObject::connect(action, &QAction::triggered, shell,
                         [invoke, label, name] {
            DrnasDiagnostics::note(QStringLiteral("TOUCH workbench ") + label);
            // Persist the phase before invoking native FreeCAD: an abort or
            // SIGSEGV bypasses C++/Python exception handlers.
            QSettings state;
            const QString key = QStringLiteral("nativeProbe/workbenchPending");
            state.setValue(key, name + QStringLiteral(":import"));
            state.sync();
            DrnasDiagnostics::note(QStringLiteral("WB checkpoint %1: import FreeCADGui begin").arg(name));
            QString error;
            if (!runOriginalFreeCADPython(QStringLiteral("import FreeCADGui as Gui"), error)) {
                state.remove(key);
                state.sync();
                DrnasDiagnostics::note(QStringLiteral("WB checkpoint %1: import FAILED: %2").arg(name, error));
                return;
            }
            DrnasDiagnostics::note(QStringLiteral("WB checkpoint %1: import succeeded").arg(name));
            // Isolate every Python operation; a native abort leaves the
            // last ENTER phase persisted for the next app launch.
            auto phase = [&](const QString& id, const QString& code) -> bool {
                state.setValue(key, name + QStringLiteral(":") + id);
                state.sync();
                DrnasDiagnostics::note(QStringLiteral("WB ISOLATE %1 %2 ENTER").arg(name, id));
                QString phaseError;
                const bool ok = runOriginalFreeCADPython(code, phaseError);
                DrnasDiagnostics::note(QStringLiteral("WB ISOLATE %1 %2 EXIT ok=%3 error=%4")
                    .arg(name, id).arg(ok).arg(phaseError));
                if (!ok) {
                    state.setValue(QStringLiteral("nativeProbe/workbenchLastError"),
                                   name + QStringLiteral(":") + id + QStringLiteral(": ") + phaseError);
                    state.remove(key); // Python error returned; not a native crash.
                    state.sync();
                }
                return ok;
            };
            if (!phase(QStringLiteral("02-listWorkbenches"),
                       QStringLiteral("import FreeCADGui as Gui\n_drnas_wbs = Gui.listWorkbenches()"))) return;
            // Android's resource tree is extracted to DRNAS_FREECAD_HOME, while
            // App.getResourceDir() may still point to a desktop-style location.
            // Put the extracted Mod directory on sys.path before importing InitGui.
            if (!phase(QStringLiteral("03-register"),
                       QStringLiteral(
                           "import os, sys, importlib, runpy, traceback\n"
                           "import FreeCADGui as Gui\n"
                           "import FreeCAD as App\n"
                           "_drnas_root = os.environ.get('DRNAS_FREECAD_HOME', '').rstrip('/')\n"
                           "_drnas_moddir = os.path.join(_drnas_root, 'Mod')\n"
                           "_drnas_module = '%2'\n"
                           "_drnas_init = os.path.join(_drnas_moddir, _drnas_module, 'InitGui.py')\n"
                           "print('DRNAS RESOURCE root=', repr(_drnas_root), 'app=', repr(App.getResourceDir()), flush=True)\n"
                           "print('DRNAS INIT path=', repr(_drnas_init), 'exists=', os.path.isfile(_drnas_init), flush=True)\n"
                           "if os.path.isdir(_drnas_moddir) and _drnas_moddir not in sys.path:\n"
                           "    sys.path.insert(0, _drnas_moddir)\n"
                           "if '%1' not in Gui.listWorkbenches():\n"
                           "    try:\n"
                           "        if not os.path.isfile(_drnas_init):\n"
                           "            raise FileNotFoundError(_drnas_init)\n"
                           "        try:\n"
                           "            importlib.import_module(_drnas_module + '.InitGui')\n"
                           "        except (ImportError, ModuleNotFoundError):\n"
                           "            runpy.run_path(_drnas_init, run_name=_drnas_module + '.InitGui')\n"
                           "    except BaseException:\n"
                           "        traceback.print_exc()\n"
                           "_drnas_registered = '%1' in Gui.listWorkbenches()\n"
                           "print('DRNAS REGISTER RESULT %1:', _drnas_registered, flush=True)\n"
                           "if not _drnas_registered:\n"
                           "    raise RuntimeError('Workbench registration failed: %1; InitGui=' + _drnas_init)\n"
                       ).arg(name, label))) return;
            if (!phase(QStringLiteral("04-activate"),
                       QStringLiteral(
                           "import FreeCADGui as Gui\n"
                           "Gui.activateWorkbench('%1')\n"
                           "print('DRNAS ACTIVATE RETURNED: %1', flush=True)\n"
                       ).arg(name))) return;
            if (label == QStringLiteral("Part"))
                phase(QStringLiteral("05-command-audit"), QStringLiteral("_drnas_part_commands()"));
            DrnasDiagnostics::note(QStringLiteral("WB ISOLATE %1 complete").arg(name));
            state.remove(key);
            state.sync();
        });
    }
    toolbar->addSeparator();

    // One compact menu button replaces six oversized desktop menu buttons.
    // Reuse the *live upstream QMenus* (including translated titles and all
    // native FreeCAD actions), rebuilt every time the button opens because
    // activating a workbench can replace MenuManager's menu objects.
    auto* menuButton = new QToolButton(toolbar);
    menuButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    menuButton->setText(QStringLiteral("Menu"));
    menuButton->setToolTip(QStringLiteral("Original FreeCAD menus"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* touchMenu = new QMenu(menuButton);
    QObject::connect(touchMenu, &QMenu::aboutToShow, shell,
                     [upstream, touchMenu] {
        DrnasDiagnostics::note(QStringLiteral("TOUCH original FreeCAD Menu"));
        touchMenu->clear();
        if (upstream && upstream->menuBar()) {
            for (QAction* action : upstream->menuBar()->actions()) {
                if (action && action->menu()) {
                    touchMenu->addMenu(action->menu());
                }
            }
        }
        if (touchMenu->actions().isEmpty()) {
            auto* unavailable = touchMenu->addAction(
                QStringLiteral("FreeCAD menus not ready"));
            unavailable->setEnabled(false);
        }
    });
    menuButton->setMenu(touchMenu);
    toolbar->addWidget(menuButton);
    auto* copyLog = toolbar->addAction(QStringLiteral("Log"));
    copyLog->setToolTip(QStringLiteral("Copy current and previous FreeCAD startup logs"));
    QObject::connect(copyLog, &QAction::triggered, shell, [report] {
        DrnasDiagnostics::note(QStringLiteral("TOUCH Copy Log (toolbar)"));
        QGuiApplication::clipboard()->setText(DrnasDiagnostics::report());
        report(QStringLiteral("Log copied to clipboard"));
    });
    toolbar->addSeparator();
    toolbar->addWidget(resultLabel);
    toolbar->setVisible(true);
    new DrnasFloatingCommands(shell, toolbar);
    new DrnasTouchInput(shell, freecad, [invoke](const QString& script) {
        invoke(script, QStringLiteral("Mouse / keyboard controls"));
    });
}

// Android QtActivity has one usable content surface. A second desktop-style
// top-level QMainWindow can end up partially covered by the Android action bar,
// with its File/View menus offscreen. Host the GENUINE upstream MainWindow as
// a child of the already-visible QtActivity shell. This is not a mock CAD UI;
// it retains FreeCAD's original menus, toolbars, docks, status bar and viewport.
bool embedOriginalFreeCADWindow(QMainWindow* shell, bool beforeFirstShow)
{
    if (!shell) return false;
    for (QWidget* candidate : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<QMainWindow*>(candidate);
        if (!window || window == shell || (!beforeFirstShow && !window->isVisible())) {
            continue;
        }
        // The upstream AndroidMainWindowBridge sets QuitOnClosed=false on
        // the *real* Gui::MainWindow immediately after constructing it.
        // At this early stage Report view and other QDockWidgets are not
        // guaranteed to exist yet; requiring that dock caused early
        // embedding to fail, spawning a second Android native window which
        // intercepted every touch above the DRNAS toolbar.
        const QVariant quitMarker = window->property("QuitOnClosed");
        if (!quitMarker.isValid() || quitMarker.toBool()) {
            continue;
        }
        qInfo() << "DRNAS matched real FreeCAD MainWindow before show:"
                << window->metaObject()->className()
                << "visible=" << window->isVisible()
                << "topLevel=" << window->isWindow()
                << "nativeHandle=" << (window->windowHandle() != nullptr);

        // Reduce oversized desktop UI furniture before Android measures it.
        QFont compactFont = window->font();
        if (compactFont.pointSizeF() > 0) {
            compactFont.setPointSizeF(std::max(7.0, compactFont.pointSizeF() * 0.80));
        } else if (compactFont.pixelSize() > 0) {
            compactFont.setPixelSize(std::max(9, compactFont.pixelSize() * 4 / 5));
        }
        window->setFont(compactFont);
        for (QToolBar* toolbar : window->findChildren<QToolBar*>()) {
            if (toolbar->iconSize() != QSize(24, 24)) toolbar->setIconSize(QSize(24, 24));
        }
        // Original menus remain alive for the Android Menu dropdown; don't
        // spend a second full-width row on the embedded desktop menu bar.
        compactOriginalAndroidFreeCADChrome(window);
        // CRITICAL: run from the native "Showing original FreeCAD MainWindow"
        // stage callback BEFORE QMainWindow::show() allocates a separate
        // Android surface / OpenGL backing store. Qt 6.4+ destroys and
        // recreates GL contexts when an initialized QOpenGLWidget switches
        // its top-level window. That caused QRhiGles2/no EGLSurface errors.
        if (!beforeFirstShow) {
            window->hide(); // Best-effort fallback; can recreate EGLSurface.
        }
        window->setParent(shell, Qt::Widget);
        window->setWindowFlags(Qt::Widget);
        window->setMinimumSize(0, 0);
        window->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        window->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        QWidget* oldCentral = shell->takeCentralWidget();
        if (oldCentral) {
            oldCentral->hide();
            // Keep bootstrap widgets alive: startup diagnostics still refer to
            // them. In the full GUI build, they contain no OpenGL preview.
            oldCentral->setParent(shell);
        }
        if (shell->menuBar()) shell->menuBar()->hide();
        if (shell->statusBar()) shell->statusBar()->hide();
        shell->setCentralWidget(window);
        shell->setWindowTitle(QStringLiteral("FreeCAD"));
        installOriginalFreeCADTouchControls(shell, window);
        if (!beforeFirstShow) {
            window->show();
        }
        // Show a recognizable version in the genuine FreeCAD status bar,
        // which is visible even when Android incorrectly overlays the menus.
        if (window->statusBar() && !window->statusBar()->findChild<QLabel*>(
                QStringLiteral("drnasAndroidBridgeMarker"))) {
            auto* marker = new QLabel(QStringLiteral("DRNAS UI 05.2"), window->statusBar());
            marker->setObjectName(QStringLiteral("drnasAndroidBridgeMarker"));
            marker->setStyleSheet(QStringLiteral(
                "QLabel{color:#ffffff;background:#275d86;padding:3px 7px;"
                "border-radius:3px;font-size:11px;}"));
            window->statusBar()->addPermanentWidget(marker);
            marker->show();
        }
        // QtActivity shell is already shown; don't showMaximized/raise it
        // during GL startup because that may recreate Android's EGLSurface.
        return true;
    }
    // Diagnostic if Qt/FreeCAD changes its startup sequence: no native
    // reparenting of unrelated Qt dialog windows as a fallback.
    qWarning() << "DRNAS upstream MainWindow not found at early embed;"
                  " expected QuitOnClosed=false set by AndroidMainWindowBridge";
    for (QWidget* candidate : QApplication::topLevelWidgets()) {
        if (auto* main = qobject_cast<QMainWindow*>(candidate)) {
            qWarning() << "DRNAS embed candidate:"
                       << main->metaObject()->className()
                       << "visible=" << main->isVisible()
                       << "quitMarker=" << main->property("QuitOnClosed")
                       << "docks=" << main->findChildren<QDockWidget*>().size();
        }
    }
    return false;
}

void recordOriginalGuiStage(const char* stageName)
{
    restoreOriginalFreeCADIconPaths();
    const QString stage = QString::fromUtf8(stageName ? stageName : "unknown");
    if (stage == QStringLiteral("Gui::Application::initApplication")) {
        // App/Python exists, but GUI InitGui scripts have not run yet.
        QString runtimeError;
        if (!runOriginalFreeCADPython(QStringLiteral(
                "import os, sys\n"
                "sys.path.insert(0, os.path.join(os.environ['DRNAS_FREECAD_HOME'], 'lib'))\n"
                "import drnas_android_runtime as _drnas_runtime\n"
                "_drnas_mark = _drnas_runtime._drnas_mark\n"
                "_drnas_storage_test = _drnas_runtime._drnas_storage_test\n"
                "_drnas_part_commands = _drnas_runtime._drnas_part_commands\n"
                "_drnas_fault_file = _drnas_runtime._drnas_fault_file\n"
                "import FreeCAD as App\n"
                "# RA11 device crash: isolate navigation decoration, retain the real CAD scene.\n"
                "_drnas_view_preferences = App.ParamGet('User parameter:BaseApp/Preferences/View')\n"
                "_drnas_view_preferences.SetBool('ShowNaviCube', False)\n"
                "_drnas_mark('RA11 ISOLATION: ShowNaviCube=False before viewer construction; main scene renderer unchanged')\n"), runtimeError)) {
            DrnasDiagnostics::note(QStringLiteral("ANDROID RUNTIME SETUP FAILED: ") + runtimeError);
        }
    }
    if (stage == QStringLiteral("Original Gui::StartupPostProcess") ||
        stage == QStringLiteral("Showing original FreeCAD MainWindow") ||
        stage.contains(QStringLiteral("OpenGL"), Qt::CaseInsensitive)) {
        DrnasDebugCenter::recordSnapshot(stage);
    }
    // Upstream StartupPostProcess::activateWorkbench() invokes
    // Gui::MainWindow::loadWindowSettings() -> show()/showMaximized().
    // The former "Showing original FreeCAD MainWindow" callback ran AFTER
    // that transition. By then FreeCAD already owns its own Android QWindow,
    // obscuring every toolbar hosted by QtActivity. Reparent just before
    // StartupPostProcess::execute(), while MainWindow is still hidden and
    // before Qt allocates FreeCAD's GL/EGL surface.
    if (stage == QStringLiteral("Original Gui::StartupPostProcess") &&
        currentFreeCADAndroidShell && !originalFreeCADEmbeddedBeforeShow) {
        originalFreeCADEmbeddedBeforeShow =
            embedOriginalFreeCADWindow(currentFreeCADAndroidShell, true);
        DrnasDebugCenter::recordSnapshot(
            QStringLiteral("after real MainWindow early embedding"));
        qInfo() << "DRNAS early QtActivity embedding:"
                << originalFreeCADEmbeddedBeforeShow;
    }
    // Late path is diagnostics/fallback only, not safe to claim pre-show.
    if (stage == QStringLiteral("Showing original FreeCAD MainWindow") &&
        currentFreeCADAndroidShell && !originalFreeCADEmbeddedBeforeShow) {
        qWarning() << "DRNAS: early embedding unavailable; FreeCAD may have"
                      " created an independent Android Qt platform window";
    }
    const QString message = QStringLiteral("ORIGINAL FreeCAD GUI: %1").arg(stage);
    DrnasDiagnostics::note(message);
    QSettings settings;
    settings.setValue(QStringLiteral("nativeProbe/current"), message);
    settings.setValue(QStringLiteral("nativeProbe/pending"), true);
    settings.setValue(QStringLiteral("nativeProbe/result"), message);
    settings.sync();
    if (currentFreeCADStartupStatus) {
        currentFreeCADStartupStatus->setText(message);
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}
}


// Android/Qt input instrumentation. This reports the actual Qt event target,
// not merely toolbar visibility or focus flags. No events are consumed:
// FreeCAD keeps its native mouse, touch, keyboard and OpenGL handling.
class DrnasInputTrace final : public QObject {
public:
    explicit DrnasInputTrace(QObject* parent) : QObject(parent) {}
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!watched || !event) return false;
        const QEvent::Type type = event->type();
        if (type != QEvent::MouseButtonPress &&
            type != QEvent::MouseButtonRelease &&
            type != QEvent::TouchBegin &&
            type != QEvent::FocusIn &&
            type != QEvent::FocusOut &&
            type != QEvent::WindowActivate &&
            type != QEvent::WindowDeactivate) return false;

        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget) return false;
        const QString klass = QString::fromLatin1(widget->metaObject()->className());
        const QString target = QStringLiteral("%1 object=%2 visible=%3 enabled=%4")
            .arg(klass, widget->objectName())
            .arg(widget->isVisible()).arg(widget->isEnabled());
        const bool input = type == QEvent::MouseButtonPress ||
                           type == QEvent::MouseButtonRelease ||
                           type == QEvent::TouchBegin;
        if (!input) {
            // Focus transitions are useful for identifying unexpected
            // top-level Qt windows, but skip noisy child focus churn.
            if (!widget->isWindow() &&
                widget->objectName() != QStringLiteral("drnasAndroidFreeCADCommands"))
                return false;
            DrnasDiagnostics::note(QStringLiteral("INPUT focus/window event=%1 %2")
                .arg(int(type)).arg(target));
            return false;
        }
        QPoint global;
        if (type == QEvent::TouchBegin) {
            auto* touch = static_cast<QTouchEvent*>(event);
            if (touch->points().isEmpty()) return false;
            global = touch->points().first().globalPosition().toPoint();
        } else {
            global = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        }
        if (auto* button = qobject_cast<QToolButton*>(widget)) {
            if (auto* action = button->defaultAction()) {
                DrnasDiagnostics::note(QStringLiteral("ACTION INPUT event=%1 name=%2 text=%3 tip=%4")
                    .arg(int(type)).arg(action->objectName(), action->text(), action->toolTip()));
            }
        }
        QWidget* hit = QApplication::widgetAt(global);
        QWidget* active = QApplication::activeWindow();
        QWidget* focus = QApplication::focusWidget();
        DrnasDiagnostics::note(
            QStringLiteral("INPUT event=%1 global=(%2,%3) receiver=[%4] "
                           "hit=[%5/%6] active=[%7] focus=[%8]")
                .arg(int(type)).arg(global.x()).arg(global.y()).arg(target)
                .arg(hit ? QString::fromLatin1(hit->metaObject()->className())
                         : QStringLiteral("null"))
                .arg(hit ? hit->objectName() : QStringLiteral("null"))
                .arg(active ? QString::fromLatin1(active->metaObject()->className())
                            : QStringLiteral("null"))
                .arg(focus ? QString::fromLatin1(focus->metaObject()->className())
                           : QStringLiteral("null")));
        return false;
    }
};

// REAL QtActivity + Qt6/C++ OpenCASCADE viewport, still NOT upstream
// FreeCADGui or App::Document/ViewProvider/Coin3D.
class DrnasQtCADShell final : public QMainWindow {
public:
    DrnasQtCADShell() {
        // Install once on the existing QApplication, before FreeCAD startup.
        // Observes presses without grabbing focus or changing rendering.
        qApp->installEventFilter(new DrnasInputTrace(qApp));
        DrnasDiagnostics::note(QStringLiteral("Installed non-consuming Qt input trace"));
#ifdef DRNAS_FREECAD_FULL_INTEGRATION
        setWindowTitle(QStringLiteral("DRNAS FreeCAD Full GUI - NO PREVIEW"));
#else
        setWindowTitle(QStringLiteral("DRNAS FreeCAD - Qt Native CAD View"));
#endif
        auto* central=new QWidget(this);
        auto* outer=new QVBoxLayout(central);
        outer->setContentsMargins(3,3,3,3);
        outer->setSpacing(3);
        central->setStyleSheet(QStringLiteral(
            "QWidget{background:#111f31;color:#e5efff;font-size:12px;}"
            "QPushButton{background:#1c4367;border:1px solid #335b78;"
            " border-radius:6px;min-height:31px;padding:2px 8px;}"
            "QPushButton:pressed{background:#357ba3;}"
            "QComboBox{background:#dcebf8;color:#0b223a;"
            " border-radius:5px;min-height:32px;}"
            "QTabWidget::pane{border:1px solid #30506c;}"
            "QTabBar::tab{background:#1d3959;color:white;padding:7px 15px;}"
            "QTabBar::tab:selected{background:#246d9c;}"));

#ifndef DRNAS_FREECAD_FULL_INTEGRATION
        auto* selectorRow=new QHBoxLayout;
        auto* brand=new QLabel(QStringLiteral("DRNAS / FreeCAD"));
        QFont font=brand->font();
        font.setBold(true);font.setPointSize(13);
        brand->setFont(font);
        selectorRow->addWidget(brand);
        selectorRow->addStretch();
        selector_=new QComboBox;
        selector_->addItems({QStringLiteral("Drilled box"),QStringLiteral("Box"),
                             QStringLiteral("Cylinder"),QStringLiteral("Sphere")});
        selector_->setMinimumWidth(136);
        selectorRow->addWidget(selector_);
        outer->addLayout(selectorRow);
#else
        // No OCCT, shaders or OpenGL bootstrap. FreeCADGui owns the graphics.
        auto* startupInfo = new QLabel(
            QStringLiteral("FULL GUI / NO PREVIEW - Qt Widgets only. "
                           "No startup OpenGL surface has been created."),
            central);
        startupInfo->setWordWrap(true);
        startupInfo->setObjectName(QStringLiteral("drnasNoPreviewMarker"));
        outer->addWidget(startupInfo);
#endif
        // Crash-resistant SAFE MODE: never attempt native FreeCAD dlopen
        // until the user explicitly presses the test button. Android native
        // linker crashes cannot be caught with a C++ try/catch.
        auto* nativeStatus = new QLabel(central);
        nativeStatus->setWordWrap(true);
        nativeStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
        nativeStatus->setStyleSheet(QStringLiteral(
            "QLabel{background:#20344a;color:#b9e7ce;padding:7px;}"));
        outer->addWidget(nativeStatus);
        QSettings diagnostics;
        QString lastStatus = diagnostics.value(
            QStringLiteral("nativeProbe/result"),
            QStringLiteral("SAFE MODE: Qt6 started. Native FreeCAD libraries are "
                           "included in the APK but have NOT been loaded.")).toString();
        if (diagnostics.value(QStringLiteral("nativeProbe/pending")).toBool()) {
            lastStatus = QStringLiteral(
                "LAST NATIVE CRASH: Android killed the app while loading %1. "
                "This information was saved before dlopen; no system crash "
                "report is needed. Restart is safe until TEST is pressed.")
                .arg(diagnostics.value(QStringLiteral("nativeProbe/current"),
                                        QStringLiteral("unknown")).toString());
            diagnostics.setValue(QStringLiteral("nativeProbe/result"), lastStatus);
            diagnostics.setValue(QStringLiteral("nativeProbe/pending"), false);
            diagnostics.sync();
        }
        nativeStatus->setText(lastStatus);
        auto* diagnosisRow=new QHBoxLayout;
        auto* runProbe=new QPushButton(QStringLiteral("TEST FREECAD LIBRARIES"));
        auto* copyDiagnostic=new QPushButton(QStringLiteral("COPY LOG"));
        runProbe->setMinimumHeight(36);
        copyDiagnostic->setMinimumHeight(36);
        diagnosisRow->addWidget(runProbe, 3);
        diagnosisRow->addWidget(copyDiagnostic, 2);
        outer->addLayout(diagnosisRow);
        // The only full upstream GUI entrypoint is exported from the *real*
        // compiled FreeCADGui.so. Leave this disabled until all native
        // libraries have loaded successfully by explicit user request.
        auto* launchOriginal=new QPushButton(
            QStringLiteral("OPEN ORIGINAL FREECAD GUI (EXPERIMENTAL)"), central);
        launchOriginal->setMinimumHeight(42);
        launchOriginal->setEnabled(false);
        outer->addWidget(launchOriginal);
#ifdef DRNAS_FREECAD_FULL_INTEGRATION
        setProperty("drnasAutoStartPartDesign", qgetenv("DRNAS_AUTOSTART") != QByteArray("0"));
#endif
        connect(launchOriginal, &QPushButton::clicked, this,
                [this, launchOriginal, nativeStatus] {
            launchOriginal->setEnabled(false);
            using StartOriginal = int (*)(void (*)(const char*));
            using ReadError = const char* (*)();
            dlerror();
            auto* start = reinterpret_cast<StartOriginal>(
                dlsym(RTLD_DEFAULT, "drnas_freecad_android_start"));
            auto* lastError = reinterpret_cast<ReadError>(
                dlsym(RTLD_DEFAULT, "drnas_freecad_android_last_error"));
            if (!start || !lastError) {
                nativeStatus->setText(QStringLiteral(
                    "This APK has no Android original-GUI bridge symbol. "
                    "Rebuild libFreeCADGui.so with AndroidMainWindowBridge."));
                launchOriginal->setEnabled(true);
                return;
            }
            QString pythonError;
            if (!configureBundledPython(pythonError)) {
                const QString message = QStringLiteral("Python bootstrap failed: %1")
                                            .arg(pythonError);
                nativeStatus->setText(message);
                QSettings settings;
                settings.setValue(QStringLiteral("nativeProbe/result"), message);
                settings.sync();
                launchOriginal->setEnabled(true);
                return;
            }
            QString resourceError;
            if (!configureBundledFreeCADHome(resourceError)) {
                const QString failed = QStringLiteral("Original FreeCAD resources: %1").arg(resourceError);
                nativeStatus->setText(failed);
                QSettings settings;
                settings.setValue(QStringLiteral("nativeProbe/result"), failed);
                settings.sync();
                launchOriginal->setEnabled(true);
                return;
            }
            currentFreeCADStartupStatus = nativeStatus;
            currentFreeCADAndroidShell = this;
            originalFreeCADEmbeddedBeforeShow = false;
            DrnasDebugCenter::recordSnapshot(
                QStringLiteral("before risky original FreeCADGui startup"));
            recordOriginalGuiStage("Calling real Gui::MainWindow startup");
            const int result = start(&recordOriginalGuiStage);
            currentFreeCADAndroidShell = nullptr;
            currentFreeCADStartupStatus = nullptr;
            // Keep the Android qss: icon root valid after startup too.
            restoreOriginalFreeCADIconPaths();
            QSettings settings;
            settings.setValue(QStringLiteral("nativeProbe/pending"), false);
            DrnasDebugCenter::recordSnapshot(
                QStringLiteral("returned from original FreeCADGui startup"));
            DrnasDiagnostics::note(QStringLiteral("Original FreeCAD startup result code: %1")
                                       .arg(result));
            if (result != 0) {
                const char* error = lastError();
                const QString reason=QString::fromUtf8(error ? error : "unknown");
                const QString failure=QStringLiteral(
                    "ORIGINAL GUI initialization failed (code %1): %2. "
                    "Restart app before retrying.").arg(result).arg(reason);
                nativeStatus->setText(failure);
                settings.setValue(QStringLiteral("nativeProbe/result"), failure);
            } else {
                const QString success=QStringLiteral(
                    "Real upstream FreeCAD Gui::MainWindow started. "
                    "Part/Sketcher/PartDesign runtime not yet verified.");
                settings.setValue(QStringLiteral("nativeProbe/result"), success);
                nativeStatus->setText(success);
                // Avoid the old show->hide->reparent sequence: EGLSurface
                // must not be recreated after Qt initialized the OpenGL FBO.
                // The native stage callback embeds before first show.
                const bool embeddedBeforeShow = originalFreeCADEmbeddedBeforeShow;
                QTimer::singleShot(0, this, [this, embeddedBeforeShow] {
                    restoreOriginalFreeCADIconPaths();
                    bool embedded = embeddedBeforeShow;
                    if (!embedded) {
                        // Legacy fallback if upstream skipped the stage hook.
                        embedded = embedOriginalFreeCADWindow(this, false);
                    }
                    if (embedded) {
                        auto* freecadWindow = qobject_cast<QMainWindow*>(centralWidget());
                        compactOriginalAndroidFreeCADChrome(freecadWindow);
                        auto* commandBar = findChild<QToolBar*>(
                            QStringLiteral("drnasAndroidFreeCADCommands"));
                        QSettings checks;
                        checks.setValue(QStringLiteral("nativeProbe/earlyEmbed"),
                                        embeddedBeforeShow);
                        checks.setValue(QStringLiteral("nativeProbe/hostBarExists"),
                                        commandBar != nullptr);
                        checks.setValue(QStringLiteral("nativeProbe/hostBarVisible"),
                                        commandBar && commandBar->isVisible());
                        checks.setValue(QStringLiteral("nativeProbe/upstreamIsWindow"),
                                        freecadWindow && freecadWindow->isWindow());
                        checks.sync();
                        qInfo() << "DRNAS Android GUI embedding diagnostics"
                                << "early=" << embeddedBeforeShow
                                << "toolbar=" << commandBar
                                << "visible=" << (commandBar && commandBar->isVisible())
                                << "native top-level="
                                << (freecadWindow && freecadWindow->isWindow());
                        DrnasDiagnostics::note(QStringLiteral(
                            "Embedded FreeCAD GUI: early=%1 toolbar=%2 visible=%3 topLevel=%4")
                            .arg(embeddedBeforeShow)
                            .arg(commandBar != nullptr)
                            .arg(commandBar && commandBar->isVisible())
                            .arg(freecadWindow && freecadWindow->isWindow()));
                        if (property("drnasAutoStartPartDesign").toBool()) {
                            setProperty("drnasAutoStartPartDesign", false);
                            DrnasDiagnostics::note(QStringLiteral("AUTO 2/3 PASS: genuine GUI embedded"));
                            QAction* partDesign = nullptr;
                            if (commandBar) {
                                for (QAction* action : commandBar->actions()) {
                                    if (action->text() == QStringLiteral("PartDesign")) {
                                        partDesign = action;
                                        break;
                                    }
                                }
                            }
                            if (partDesign) {
                                bool modulesReady = true;
                                for (const auto& probe : {
                                         std::pair<const char*, const char*>{"TEST 1 Part", "import Part"},
                                         {"TEST 2 PartGui", "import PartGui"}}) {
                                    const QString caption = QString::fromLatin1(probe.first);
                                    DrnasDiagnostics::note(QStringLiteral("AUTO ") + caption + QStringLiteral(" BEGIN"));
                                    QString importError;
                                    if (!runOriginalFreeCADPython(QString::fromLatin1(probe.second), importError)) {
                                        DrnasDiagnostics::note(QStringLiteral("AUTO ") + caption + QStringLiteral(" FAILED: ") + importError);
                                        modulesReady = false;
                                        break;
                                    }
                                    DrnasDiagnostics::note(QStringLiteral("AUTO ") + caption + QStringLiteral(" PASS"));
                                }
                                DrnasDiagnostics::note(QStringLiteral("AUTO TEST 3: activate PartDesignWorkbench"));
                                if (modulesReady) partDesign->trigger(); // real registration/activation checkpoints
                                QString error;
                                const bool active = modulesReady && runOriginalFreeCADPython(QStringLiteral(
                                    "import FreeCADGui as Gui\n"
                                    "if Gui.activeWorkbench().name() != 'PartDesignWorkbench':\n"
                                    "    raise RuntimeError('PartDesignWorkbench is not active')\n"), error);
                                DrnasDiagnostics::note(active
                                    ? QStringLiteral("AUTO 3/3 PASS: PartDesignWorkbench active")
                                    : QStringLiteral("AUTO 3/3 FAILED: ") + error);
                            } else {
                                DrnasDiagnostics::note(QStringLiteral("AUTO 3/3 FAILED: PartDesign action unavailable"));
                            }
                        }
                        // Do not resurrect the duplicate desktop menu row.
                        compactOriginalAndroidFreeCADChrome(freecadWindow);
                        // FreeCAD's deferred restoreState() can run later.
                        // Re-apply after it without touching the EGLSurface.
                        QPointer<QMainWindow> guarded(freecadWindow);
                        QTimer::singleShot(250, this, [guarded] {
                            if (guarded) compactOriginalAndroidFreeCADChrome(guarded.data());
                        });
                    } else {
                        QSettings fallback;
                        fallback.setValue(QStringLiteral("nativeProbe/result"),
                            QStringLiteral("FreeCAD started but original MainWindow embedding failed"));
                        fallback.sync();
                    }
                });
            }
            settings.sync();
        });
        connect(copyDiagnostic, &QPushButton::clicked, this, [nativeStatus] {
            DrnasDiagnostics::note(QStringLiteral("TOUCH Copy Log (safe mode)"));
            QGuiApplication::clipboard()->setText(DrnasDiagnostics::report());
            nativeStatus->setText(QStringLiteral(
                "Copied previous and current startup logs. Paste into ChatGPT."));
        });
        connect(runProbe, &QPushButton::clicked, this, [nativeStatus, runProbe, launchOriginal] {
            // Save a durable breadcrumb for the next launch *before* each
            // potentially crashing dlopen. No auto-retry on next launch.
            constexpr const char* libraries[] = {
                "libGL.so", "libGLU.so", "libCoin.so",
                "libFreeCADBase.so", "libFreeCADApp.so", "libFreeCADGui.so"
            };
            runProbe->setEnabled(false);
            launchOriginal->setEnabled(false);
            QSettings diag;
            for (const char* library : libraries) {
                const QString libName=QString::fromLatin1(library);
                const QString progress=QStringLiteral("Loading %1 ...").arg(libName);
                DrnasDiagnostics::note(progress);
                nativeStatus->setText(progress);
                diag.setValue(QStringLiteral("nativeProbe/current"), libName);
                diag.setValue(QStringLiteral("nativeProbe/pending"), true);
                diag.setValue(QStringLiteral("nativeProbe/result"), progress);
                diag.sync();
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                dlerror();  // clear pre-existing error
                if (!dlopen(library, RTLD_NOW | RTLD_GLOBAL)) {
                    const char* error=dlerror();
                    const QString detail=QStringLiteral("dlopen FAILED for %1: %2")
                        .arg(libName)
                        .arg(QString::fromLocal8Bit(error ? error : "unknown error"));
                    DrnasDiagnostics::note(detail);
                    diag.setValue(QStringLiteral("nativeProbe/result"), detail);
                    diag.setValue(QStringLiteral("nativeProbe/pending"), false);
                    diag.sync();
                    nativeStatus->setText(detail);
                    runProbe->setEnabled(true);
                    return;
                }
            }
            DrnasDiagnostics::note(QStringLiteral("All FreeCAD libraries loaded successfully"));
            const QString success=QStringLiteral(
                "PASS: genuine FreeCADBase, FreeCADApp, FreeCADGui, Coin3D, "
                "GL4ES/GLU loaded. Original FreeCAD desktop GUI NOT yet running.");
            diag.setValue(QStringLiteral("nativeProbe/result"), success);
            diag.setValue(QStringLiteral("nativeProbe/pending"), false);
            diag.sync();
            nativeStatus->setText(success);
            runProbe->setEnabled(true);
            launchOriginal->setEnabled(true);
        });
#ifdef DRNAS_FREECAD_FULL_INTEGRATION
        if (property("drnasAutoStartPartDesign").toBool()) {
            // Wait until the QtActivity surface and event loop are ready.
            // Reuse actual button handlers; never fabricate PASS results.
            QTimer::singleShot(350, this, [this, runProbe, launchOriginal] {
                DrnasDiagnostics::note(QStringLiteral("AUTO 1/3 BEGIN: load genuine FreeCAD libraries; QT_SCALE_FACTOR=%1")
                    .arg(QString::fromLatin1(qgetenv("QT_SCALE_FACTOR"))));
                if (auto* screen = QGuiApplication::primaryScreen()) {
                    DrnasDiagnostics::note(QStringLiteral("STARTUP display logical=%1x%2 dpr=%3")
                        .arg(screen->geometry().width()).arg(screen->geometry().height())
                        .arg(screen->devicePixelRatio()));
                }
                runProbe->click();
                if (!launchOriginal->isEnabled()) {
                    setProperty("drnasAutoStartPartDesign", false);
                    DrnasDiagnostics::note(QStringLiteral("AUTO 1/3 FAILED: GUI launch remains disabled"));
                    return;
                }
                DrnasDiagnostics::note(QStringLiteral("AUTO 1/3 PASS: libraries loaded"));
                DrnasDiagnostics::note(QStringLiteral("AUTO 2/3 BEGIN: original FreeCAD GUI"));
                launchOriginal->click();
            });
        }
        auto* plainStatus = new QLabel(
            QStringLiteral("NO PREVIEW - original FreeCAD owns the CAD graphics surface."),
            central);
        plainStatus->setWordWrap(true);
        plainStatus->setObjectName(QStringLiteral("drnasNoGLStatus"));
        outer->addWidget(plainStatus);
        outer->addStretch(1);
#else
        auto* integrationState = new QLabel(
            QStringLiteral("QtActivity stable-mode OCCT viewer / native FreeCAD "
                           "load test is manual; original workbenches pending."),
            central);
        integrationState->setWordWrap(true);
        outer->addWidget(integrationState);

        auto* buttonRow=new QHBoxLayout;
        buttonRow->setSpacing(3);
        for(int i=0;i<4;i++) {
            const int preset=i;
            const char* labels[]={"ISO","TOP","FRONT","RIGHT"};
            auto* button=new QPushButton(QString::fromLatin1(labels[i]));
            buttonRow->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,preset](){
                viewport_->setView(preset);
            });
        }
        auto* fit=new QPushButton(QStringLiteral("FIT"));
        buttonRow->addWidget(fit);
        connect(fit,&QPushButton::clicked,this,[this](){viewport_->fit();});
        outer->addLayout(buttonRow);

        viewport_=new NativeOcctViewport(central);
        viewport_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        outer->addWidget(viewport_,1);

        auto* toggles=new QHBoxLayout;
        auto* grid=new QPushButton(QStringLiteral("GRID ON"));
        auto* edges=new QPushButton(QStringLiteral("EDGES OFF"));
        toggles->addWidget(grid);
        toggles->addWidget(edges);
        connect(grid,&QPushButton::clicked,this,[this,grid](){
            grid_=!grid_;
            viewport_->setGrid(grid_);
            grid->setText(grid_?QStringLiteral("GRID ON"):QStringLiteral("GRID OFF"));
        });
        connect(edges,&QPushButton::clicked,this,[this,edges](){
            edges_=!edges_;
            viewport_->setWire(edges_);
            edges->setText(edges_?QStringLiteral("EDGES ON"):QStringLiteral("EDGES OFF"));
        });
        outer->addLayout(toggles);

        auto* tabs=new QTabWidget(central);
        tree_=new QTreeWidget(tabs);
        tree_->setHeaderLabel(QStringLiteral("Preview model / B-Rep"));
        tree_->setStyleSheet(QStringLiteral("QTreeWidget{background:#162b43;color:#edf4ff;}"));
        tabs->addTab(tree_,QStringLiteral("MODEL"));
        info_=new QLabel;
        info_->setWordWrap(true);
        info_->setAlignment(Qt::AlignLeft|Qt::AlignTop);
        info_->setContentsMargins(9,6,9,2);
        tabs->addTab(info_,QStringLiteral("PROPERTIES"));
        tabs->setMaximumHeight(144);
        outer->addWidget(tabs);
#endif

        setCentralWidget(central);
#ifndef DRNAS_FREECAD_FULL_INTEGRATION
        auto* fileMenu=menuBar()->addMenu(QStringLiteral("File"));
        auto* save=fileMenu->addAction(QStringLiteral("Save FCStd (integration pending)"));
        save->setEnabled(false);
        auto* viewMenu=menuBar()->addMenu(QStringLiteral("View"));
        QAction* iso=viewMenu->addAction(QStringLiteral("Isometric"));
        connect(iso,&QAction::triggered,this,[this](){viewport_->setView(0);});
        auto* showModel=viewMenu->addAction(QStringLiteral("Model and properties"));
        connect(showModel,&QAction::triggered,tabs,[tabs](){tabs->setCurrentIndex(0);});

        connect(selector_,QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,[this](int shape){selectShape(shape);});
        updateModel(0);
#else
        menuBar()->setNativeMenuBar(false);
        menuBar()->hide();
#endif
        statusBar()->setStyleSheet(QStringLiteral(
                "QStatusBar{background:#142b46;color:#b3d7f5;}"));
#ifdef DRNAS_FREECAD_FULL_INTEGRATION
        statusBar()->showMessage(QStringLiteral(
            "NO PREVIEW / Qt Widgets only - original FreeCAD GL starts on OPEN GUI"));
#else
        statusBar()->showMessage(QStringLiteral(
             "Real native OCCT 3D viewport  •  FreeCADGui documents: next stage"));
#endif
        const QRect available=QGuiApplication::primaryScreen()->availableGeometry();
        resize(std::min(available.width(),950),std::min(available.height(),1400));
    }
private:
#ifndef DRNAS_FREECAD_FULL_INTEGRATION
    void selectShape(int kind) {
        if(!viewport_->setShape(kind)) {
            statusBar()->showMessage(QStringLiteral("Native OpenCASCADE shape failed"));
            return;
        }
        viewport_->fit();
        updateModel(kind);
    }
    void updateModel(int kind) {
        const QString names[]={QStringLiteral("Drilled Box 10x20x30 mm"),
             QStringLiteral("Solid Box 20x20x20 mm"),
             QStringLiteral("Cylinder D16x24 mm"),
             QStringLiteral("Sphere D20 mm")};
        tree_->clear();
        auto* project=new QTreeWidgetItem(tree_);
        project->setText(0,QStringLiteral("Native OCCT model"));
        auto* node=new QTreeWidgetItem(project);
        node->setText(0,names[std::clamp(kind,0,3)]);
        project->setExpanded(true);
        info_->setText(
            QStringLiteral("Geometry: REAL OpenCASCADE B-Rep\n"
                           "Object: %1\n"
                           "Triangles: %2\n"
                           "Rendering: Qt6 OpenGL ES on Android\n"
                           "FreeCAD App document integration: pending")
                .arg(names[std::clamp(kind,0,3)])
                .arg(viewport_->triangleCount()));
        statusBar()->showMessage(
            QStringLiteral("%1  -  native OCCT B-Rep / %2 triangles")
                .arg(names[std::clamp(kind,0,3)])
                .arg(viewport_->triangleCount()));
    }
    NativeOcctViewport* viewport_{nullptr};
    QComboBox* selector_{nullptr};
    QTreeWidget* tree_{nullptr};
    QLabel* info_{nullptr};
    bool grid_{true},edges_{false};
#endif
};

extern "C" __attribute__((visibility("default")))
void* drnas_create_qt_gui_shell_after_qt_android_bootstrap() {
    return new DrnasQtCADShell();
}
