#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QOpenGLWidget>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QToolBar>
#include <QTreeWidget>
#include <QSvgRenderer>

// This is a genuine Qt Widgets/OpenGLWidgets cross-link milestone, not the
// upstream FreeCADGui or a FreeCAD document. QtActivity must initialize a
// QApplication before this class may be constructed on a device.
class DrnasQtCADShell final : public QMainWindow {
public:
    DrnasQtCADShell() {
        setWindowTitle(QStringLiteral("DRNAS FreeCAD native GUI arm64"));
        resize(1100, 740);
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        auto *view = new QOpenGLWidget(this);
        view->setFormat(format);
        setCentralWidget(view);
        auto *model = new QDockWidget(QStringLiteral("Model"), this);
        auto *tree = new QTreeWidget(model);
        tree->setHeaderLabel(QStringLiteral("Document objects"));
        model->setWidget(tree);
        addDockWidget(Qt::LeftDockWidgetArea, model);
        auto *properties = new QDockWidget(QStringLiteral("Properties"), this);
        properties->setWidget(new QLabel(QStringLiteral(
            "Real FreeCAD document integration pending"), properties));
        addDockWidget(Qt::RightDockWidgetArea, properties);
        auto *toolbar = addToolBar(QStringLiteral("CAD"));
        for(const auto& name : {QStringLiteral("New"), QStringLiteral("Open"),
                                QStringLiteral("Save"), QStringLiteral("Sketcher"),
                                QStringLiteral("Part Design")}) {
            auto *a = toolbar->addAction(name);
            a->setEnabled(false); // never pretend the App::Document adapter exists
        }
        menuBar()->addMenu(QStringLiteral("File"));
        menuBar()->addMenu(QStringLiteral("View"));
        statusBar()->showMessage(QStringLiteral("Qt Android GUI milestone"));
        QSvgRenderer svg(QByteArray(
            "<svg xmlns='http://www.w3.org/2000/svg' width='1' height='1'/>"));
        Q_UNUSED(svg);
    }
};

extern "C" __attribute__((visibility("default")))
void* drnas_create_qt_gui_shell_after_qt_android_bootstrap() {
    return new DrnasQtCADShell();
}
