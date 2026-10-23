#!/usr/bin/env python3
"""Trace the original document/viewport path without replacing CAD commands."""
from pathlib import Path
import sys


def once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Pinned document source changed: {old!r}")
    return source.replace(old, new, 1)


def annotate(root, path, signature, phases):
    target = root / path
    source = target.read_text()
    start = source.index(signature)
    opening = source.index("\n{", start) + 2
    closing = source.index("\n}", opening)
    body = source[opening:closing]
    for statement, phase in phases:
        body = once(body, statement,
                    f'::drnasDocumentPhase("{phase}", "BEGIN");\n    '
                    + statement + f'\n    ::drnasDocumentPhase("{phase}", "END");')
    body = '\n    ::DrnasDocumentTraceScope _drnasDocumentTrace("' + signature.split("(")[0] + '");' + body
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
    explicit DrnasDocumentTraceScope(const char* name)
        : phase(name), exceptions(std::uncaught_exceptions())
    {
        drnasDocumentPhase(phase, "ENTER");
    }
    ~DrnasDocumentTraceScope()
    {
        drnasDocumentPhase(phase, std::uncaught_exceptions() > exceptions ? "UNWIND" : "EXIT");
    }
private:
    const char* phase;
    int exceptions;
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


if __name__ == "__main__":
    patch(Path(sys.argv[1]))
    print("Original Std_New, App/GUI observers and Quarter/Coin viewport now have durable native checkpoints")
