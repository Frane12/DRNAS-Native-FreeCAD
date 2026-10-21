#include "NativeOcctViewport.h"
#include <algorithm>
#include <QFont>
#include <QSizePolicy>
#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QEventLoop>
#include <QDir>
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
#include <QScreen>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabWidget>
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
void recordOriginalGuiStage(const char* stageName)
{
    const QString stage = QString::fromUtf8(stageName ? stageName : "unknown");
    const QString message = QStringLiteral("ORIGINAL FreeCAD GUI: %1").arg(stage);
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

// REAL QtActivity + Qt6/C++ OpenCASCADE viewport, still NOT upstream
// FreeCADGui or App::Document/ViewProvider/Coin3D.
class DrnasQtCADShell final : public QMainWindow {
public:
    DrnasQtCADShell() {
        setWindowTitle(QStringLiteral("DRNAS FreeCAD - Qt Native CAD View"));
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
        auto* copyDiagnostic=new QPushButton(QStringLiteral("COPY ERROR"));
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
            recordOriginalGuiStage("Calling real Gui::MainWindow startup");
            const int result = start(&recordOriginalGuiStage);
            QSettings settings;
            settings.setValue(QStringLiteral("nativeProbe/pending"), false);
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
                this->hide(); // The upstream FreeCAD QMainWindow is now shown.
            }
            settings.sync();
        });
        connect(copyDiagnostic, &QPushButton::clicked, this, [nativeStatus] {
            QGuiApplication::clipboard()->setText(nativeStatus->text());
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
                    diag.setValue(QStringLiteral("nativeProbe/result"), detail);
                    diag.setValue(QStringLiteral("nativeProbe/pending"), false);
                    diag.sync();
                    nativeStatus->setText(detail);
                    runProbe->setEnabled(true);
                    return;
                }
            }
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

        setCentralWidget(central);
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
        statusBar()->setStyleSheet(QStringLiteral(
                "QStatusBar{background:#142b46;color:#b3d7f5;}"));
        statusBar()->showMessage(QStringLiteral(
             "Real native OCCT 3D viewport  •  FreeCADGui documents: next stage"));
        const QRect available=QGuiApplication::primaryScreen()->availableGeometry();
        resize(std::min(available.width(),950),std::min(available.height(),1400));
    }
private:
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
};

extern "C" __attribute__((visibility("default")))
void* drnas_create_qt_gui_shell_after_qt_android_bootstrap() {
    return new DrnasQtCADShell();
}
