#!/usr/bin/env python3
"""Trace the original document/viewport path without replacing CAD commands."""
from pathlib import Path
import re
import sys


def once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Pinned document source changed: {old!r}")
    return source.replace(old, new, 1)


def annotate(root, path, signature, phases, max_calls=None):
    target = root / path
    source = target.read_text()
    start = source.index(signature)
    brace = re.search(r"\n([ \t]*)\{", source[start:])
    if brace is None:
        raise RuntimeError(f"Missing function body: {signature}")
    opening = start + brace.end()
    closing = source.index("\n" + brace.group(1) + "}", opening)
    body = source[opening:closing]
    for statement, phase in phases:
        body = once(body, statement,
                    f'if (_drnasTraceEnabled) ::drnasDocumentPhase("{phase}", "BEGIN");\n    '
                    + statement + f'\n    if (_drnasTraceEnabled) ::drnasDocumentPhase("{phase}", "END");')
    prefix = '\n    const bool _drnasTraceEnabled = true;'
    if max_calls is not None:
        prefix = (f'\n    static unsigned int _drnasTraceCalls = 0;'
                  f'\n    const bool _drnasTraceEnabled = _drnasTraceCalls < {max_calls};'
                  '\n    if (_drnasTraceEnabled) ++_drnasTraceCalls;')
    body = prefix + '\n    ::DrnasDocumentTraceScope _drnasDocumentTrace("' + signature.split("(")[0] + '", _drnasTraceEnabled);' + body
    source = source[:opening] + body + source[closing:]
    include = '#include <Base/AndroidDocumentDiagnostics.h>\n'
    if include not in source:
        source = include + source
    target.write_text(source)


def patch(root):
    # No Python/Qt calls: durable native markers still work during a failure
    # below Python. This is ordinary execution logging, not a signal handler.
    (root / "src/Base/AndroidDocumentDiagnostics.h").write_text(r'''#pragma once
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <unistd.h>

inline void drnasDocumentPhase(const char* phase, const char* event)
{
    std::fprintf(stderr, "DRNAS_NEW_NATIVE %s %s\n", phase, event);
    std::fflush(stderr);
    const char* path = std::getenv("DRNAS_WB_LOG_PATH");
    if (path && *path) {
        if (FILE* file = std::fopen(path, "a")) {
            std::fprintf(file, "[NEW_NATIVE] %s %s\n", phase, event);
            std::fflush(file);
            ::fsync(::fileno(file));
            std::fclose(file);
        }
    }
}

class DrnasDocumentTraceScope
{
public:
    explicit DrnasDocumentTraceScope(const char* name, bool shouldTrace = true)
        : phase(name), exceptions(std::uncaught_exceptions()), enabled(shouldTrace)
    {
        if (enabled) drnasDocumentPhase(phase, "ENTER");
    }
    ~DrnasDocumentTraceScope()
    {
        if (enabled) drnasDocumentPhase(phase, std::uncaught_exceptions() > exceptions ? "UNWIND" : "EXIT");
    }
private:
    const char* phase;
    int exceptions;
    bool enabled;
};
''')
    annotate(root, "src/Gui/CommandDoc.cpp", "void StdCmdNew::activated(int iMsg)", [
        ('runCommand(Command::Doc, cmd.toUtf8());', 'D01 App.newDocument command'),
        ('doCommand(Command::Gui, "Gui.activeDocument().activeView().viewDefaultOrientation()");', 'D02 default orientation'),
        ('doCommand(Command::Gui, "Gui.ActiveDocument.ActiveView.setAxisCross(True)");', 'D03 axis cross'),
    ])
    annotate(root, "src/App/Application.cpp", "Document* Application::newDocument(", [
        ('auto doc = new Document(name.c_str());', 'A01 App Document constructor'),
        ('setActiveDocumentNoSignal(doc);', 'A02 set active document'),
        ('signalNewDocument(*doc, CreateFlags.createView);', 'A03 notify new document observers'),
        ('doc->Label.setValue(label);', 'A04 set label'),
    ])
    annotate(root, "src/Gui/Application.cpp", "void Application::slotNewDocument(", [
        ('auto pDoc = new Gui::Document(const_cast<App::Document*>(&Doc), this);', 'G01 Gui Document constructor'),
        ('signalNewDocument(*pDoc, isMainDoc);', 'G02 notify GUI document observers'),
        ('pDoc->createView(View3DInventor::getClassTypeId());', 'G03 create 3D view'),
    ])
    annotate(root, "src/Gui/Document.cpp", "MDIView* Document::createView(", [
        ('auto view3D = new View3DInventor(this, getMainWindow(), shareWidget);', 'V01 View3DInventor constructor'),
        ('getMainWindow()->addWindow(view3D);', 'V02 add MDI window'),
        ('view3D->getViewer()->redraw();', 'V03 redraw'),
    ])
    annotate(root, "src/Gui/View3DInventor.cpp", "View3DInventor::View3DInventor(", [
        ('_viewer = new View3DInventorViewer(f, this, sharewidget);', 'I01 viewer with format'),
        ('_viewer = new View3DInventorViewer(this, sharewidget);', 'I02 default viewer'),
        ('_viewer->setDocument(this->_pcDocument);', 'I03 attach document'),
        ('applySettings();', 'I04 apply view settings'),
    ])
    annotate(root, "src/Gui/View3DInventorViewer.cpp", "void View3DInventorViewer::init()", [
        ('initialize();', 'R01 initialize viewer'),
        ('auto cam = new SoOrthographicCamera;', 'R02 Coin camera'),
        ('SoDirectionalLight* hl = this->getHeadlight();', 'R03 get headlight'),
        ('setSceneGraph(viewerSceneRoot);', 'R04 set scene graph'),
    ])
    annotate(root, "src/Gui/Quarter/QuarterWidget.cpp", "QuarterWidget::constructor(", [
        ('setViewport(new CustomGLWidget(format, this, sharewidget));', 'Q01 CustomGLWidget constructor'),
        ('PRIVATE(this) = new QuarterWidgetP(this, sharewidget);', 'Q02 Quarter private constructor'),
        ('PRIVATE(this)->sorendermanager = new SoRenderManager;', 'Q03 Coin render manager'),
        ('PRIVATE(this)->soeventmanager = new SoEventManager;', 'Q04 Coin event manager'),
        ('PRIVATE(this)->headlight = new SoDirectionalLight;', 'Q05 Coin headlight'),
        ('PRIVATE(this)->sorendermanager->activate();', 'Q06 activate render manager'),
    ])
    # The physical-device trace reaches V02 BEGIN and crashes before V02 END.
    # Keep real MDI behavior; split the failing call and synchronous callbacks.
    annotate(root, "src/Gui/MainWindow.cpp", "void MainWindow::addWindow(MDIView* view)", [
        ('bool isempty = d->mdiArea->subWindowList().isEmpty();', 'M01 inspect MDI area'),
        ('auto child = qobject_cast<QMdiSubWindow*>(view->parentWidget());', 'M02 inspect view parent'),
        ('child = new QMdiSubWindow(d->mdiArea->viewport());', 'M03 create subwindow'),
        ('child->setAttribute(Qt::WA_DeleteOnClose);', 'M04 set close attribute'),
        ('child->setWidget(view);', 'M05 reparent view into subwindow'),
        ('child->setWindowIcon(view->windowIcon());', 'M06 set subwindow icon'),
        ('QMenu* menu = child->systemMenu();', 'M07 obtain system menu'),
        ('QList<QAction*> acts = menu->actions();', 'M08 read system menu actions'),
        ('QAction* action = menu->addAction(tr("Close All"));', 'M09 add close action'),
        ('d->mdiArea->addSubWindow(child);', 'M10 add subwindow to MDI area'),
        ('view->installEventFilter(this);', 'M11 install view event filter'),
        ('view->showMaximized();', 'M12 show first view maximized'),
        ('view->show();', 'M13 show subsequent view'),
    ])
    annotate(root, "src/Gui/MainWindow.cpp", "void MainWindow::onWindowActivated(QMdiSubWindow* mdi)", [
        ('auto view = dynamic_cast<MDIView*>(mdi->widget());', 'W01 resolve activated view'),
        ('setActiveWindow(view);', 'W02 activate MDI view'),
    ])
    annotate(root, "src/Gui/MainWindow.cpp", "void MainWindow::setActiveWindow(MDIView* view)", [
        ('view->setFocus();', 'S01 focus view'),
        ('d->mdiArea->setActiveSubWindow(subwindow);', 'S02 activate Qt subwindow'),
        ('Application::Instance->viewActivated(view);', 'S03 notify FreeCAD active view'),
        ('updateActions();', 'S04 refresh command availability'),
    ])
    annotate(root, "src/Gui/Application.cpp", "void Application::viewActivated(MDIView* pcView)", [
        ('signalActivateView(pcView);', 'C01 notify active view observers'),
        ('getMainWindow()->setWindowTitle(pcView->buildWindowTitle());', 'C02 update window title'),
        ('getMainWindow()->setWindowModified(document->isModified());', 'C03 update modified state'),
        ('setActiveDocument(pcView->getGuiDocument());', 'C04 activate GUI document'),
    ])
    # Paint paths can run continuously. Only journal their first two calls.
    annotate(root, "src/Gui/Quarter/QuarterWidget.cpp", "void initializeGL() override", [
        ('connect(this, &CustomGLWidget::resized, this, &CustomGLWidget::slotResized);', 'GL01 initialize GL widget'),
    ], max_calls=2)
    annotate(root, "src/Gui/Quarter/QuarterWidget.cpp", "void paintGL() override", [
        ('qw->redraw();', 'GL02 GL widget redraw'),
    ], max_calls=2)
    annotate(root, "src/Gui/Quarter/QuarterWidget.cpp", "QuarterWidget::paintEvent(QPaintEvent* event)", [
        ('this->getSoRenderManager()->reinitialize();', 'P01 reinitialize Coin render manager'),
        ('getSoRenderManager()->activate();', 'P02 activate Coin for paint'),
        ('this->actualRedraw();', 'P03 render Coin scene'),
        ('inherited::paintEvent(event);', 'P04 paint graphics view'),
    ], max_calls=2)

    # Diagnostic APK: skip the crashing Coin3D draw, not document creation.
    # This intentionally leaves the 3D viewport blank while isolating P03.
    quarter_path = root / "src/Gui/Quarter/QuarterWidget.cpp"
    quarter_source = quarter_path.read_text()
    quarter_source = once(quarter_source,
                          'this->actualRedraw();',
                          '::drnasDocumentPhase("P03 Coin scene render", "SKIPPED_ANDROID_DIAGNOSTIC");')
    quarter_path.write_text(quarter_source)


if __name__ == "__main__":
    patch(Path(sys.argv[1]))
    print("MDI trace v2: original Std_New, MDI attachment/activation and first GL paints have durable native checkpoints")
