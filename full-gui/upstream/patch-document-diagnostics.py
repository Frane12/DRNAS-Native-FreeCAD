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


def patch_android_service_storage(root):
    # Registry keys already validate the exact interface name across modules.
    # Hidden interface RTTI in Android DSOs can make std::any_cast<T*> reject
    # an otherwise matching registered pointer. Store the converted interface
    # pointer directly; do not repeat the RTTI check across the DSO boundary.
    path = root / "src/Base/ServiceProvider.h"
    source = path.read_text()
    source = once(source, '        std::any instance;',
                  '''#ifdef __ANDROID__
        void* instance;
#else
        std::any instance;
#endif''')
    source = once(source, '            return std::any_cast<T*>(instance);',
                  '''#ifdef __ANDROID__
            return static_cast<T*>(instance);
#else
            return std::any_cast<T*>(instance);
#endif''')
    path.write_text(source)


def patch(root):
    patch_android_service_storage(root)
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

    # P03 dispatches virtually into View3DInventorViewer, not directly into
    # QuarterWidget::actualRedraw. Trace background Coin traversal separately.
    annotate(root, "src/Gui/View3DInventorViewer.cpp", "void View3DInventorViewer::actualRedraw()", [
        ('renderScene();', 'RV01 native renderScene'),
        ('renderFramebuffer();', 'RV02 framebuffer render'),
        ('renderGLImage();', 'RV03 image render'),
    ], max_calls=2)
    annotate(root, "src/Gui/View3DInventorViewer.cpp", "void View3DInventorViewer::renderScene()", [
        ('glViewport(origin[0], origin[1], size[0], size[1]);', 'RS01 GL viewport'),
        ('glClearColor(float(col.redF()), float(col.greenF()), float(col.blueF()), 0.0F);', 'RS02 GL clear color'),
        ('glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);', 'RS03 GL clear buffers'),
        ('glEnable(GL_DEPTH_TEST);', 'RS04 GL depth test'),
        ('this->renderGLActionScene(col, this->getSoRenderManager()->getGLRenderAction());', 'RS05 background and main Coin actions'),
        ('this->drawAxisCross();', 'RS05A axis cross'),
        ('printDimension();', 'RS05B dimension overlay'),
        ('it->paintGL();', 'RS05C graphics item'),
        ('renderRubberbandOverlay();', 'RS06 rubberband overlay'),
    ], max_calls=2)
    annotate(root, "src/Gui/View3DInventorViewer.cpp", "void View3DInventorViewer::renderGLActionScene(", [
        ('SoState* state = glra->getState();', 'RA01 obtain Coin action state'),
        ('SoDevicePixelRatioElement::set(state, devicePixelRatio());', 'RA02 device pixel ratio'),
        ('SoGLWidgetElement::set(state, qobject_cast<QOpenGLWidget*>(this->getGLWidget()));', 'RA03 GL widget element'),
        ('SoGLRenderActionElement::set(state, glra);', 'RA04 GL action element'),
        ('SoGLVBOActivatedElement::set(state, this->vboEnabled);', 'RA05 VBO element'),
        ('drawSingleBackground(backgroundColor);', 'RA06 solid background overlay'),
        ('glra->apply(this->backgroundroot);', 'RA07 background scene traversal'),
        ('inherited::actualRedraw();', 'RA08 main scene render manager'),
        ('renderDelayedAnnotations(glra);', 'RA09 delayed annotations'),
        ('glra->apply(this->foregroundroot);', 'RA10 foreground traversal'),
        ('glra->apply(this->decorationroot);', 'RA11 decoration traversal'),
    ], max_calls=2)
    annotate(root, "src/Gui/View3DInventorViewer.cpp", "void applyOverlay(", [
        ('SoGLRenderAction action(SbViewportRegion(viewportWidth, viewportHeight));', 'OV01 create overlay action'),
        ('setOverlayCacheContext(action, viewer);', 'OV02 set overlay cache context'),
        ('action.setTransparencyType(SoGLRenderAction::BLEND);', 'OV03 overlay transparency'),
        ('action.apply(root);', 'OV04 overlay Coin traversal'),
        ('resetMainLazyGLState(viewer);', 'OV05 reset main Coin GL state'),
    ], max_calls=2)
    annotate(root, "src/Gui/Quarter/QuarterWidget.cpp", "QuarterWidget::actualRedraw()", [
        ('PRIVATE(this)->sorendermanager->render(PRIVATE(this)->clearwindow,\n                                         PRIVATE(this)->clearzbuffer);', 'RM01 Coin render manager'),
    ], max_calls=2)

    viewer_path = root / "src/Gui/View3DInventorViewer.cpp"
    viewer_source = viewer_path.read_text()
    # Device A and B both pass the main Coin scene and crash inside
    # drawAxisCross(). Isolate only this auxiliary draw on Android; leave
    # desktop drawing and the genuine scene renderer unchanged.
    viewer_source = once(viewer_source,
        'if (_drnasTraceEnabled) ::drnasDocumentPhase("RS05A axis cross", "BEGIN");\n    this->drawAxisCross();',
        r'''if (_drnasTraceEnabled) ::drnasDocumentPhase("RS05A axis cross", "BEGIN");
#ifdef __ANDROID__
    if (_drnasTraceEnabled)
        ::drnasDocumentPhase("RS05A axis cross", "SKIPPED_ANDROID_ISOLATION");
#else
    this->drawAxisCross();
#endif
''')
    gpu_probe = r'''
#ifdef __ANDROID__
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <dlfcn.h>

// Queries only: preserve the selected rendering mode and all GL state.
static void drnasRenderGpuSnapshot(QOpenGLWidget* widget)
{
    ::DrnasDocumentTraceScope scope("GPU00 render context snapshot");
    QOpenGLContext* context = QOpenGLContext::currentContext();
    char line[1536];
    std::snprintf(line, sizeof(line),
        "GPU01 context current=%p widgetContext=%p valid=%d GLES=%d",
        static_cast<void*>(context), static_cast<void*>(widget ? widget->context() : nullptr),
        context && context->isValid(), context && context->isOpenGLES());
    ::drnasDocumentPhase(line, "SNAPSHOT");
    if (!context || !context->isValid()) return;
    QOpenGLFunctions* functions = context->functions();
    if (!functions) return;
    GLint bound = -1, depth = -1, stencil = -1, viewport[4] = {};
    functions->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
    functions->glGetIntegerv(GL_DEPTH_BITS, &depth);
    functions->glGetIntegerv(GL_STENCIL_BITS, &stencil);
    functions->glGetIntegerv(GL_VIEWPORT, viewport);
    std::snprintf(line, sizeof(line),
        "GPU02 FBO bound=%d widget=%u depth=%d stencil=%d viewport=%d,%d,%d,%d",
        bound, widget ? widget->defaultFramebufferObject() : 0u, depth, stencil,
        viewport[0], viewport[1], viewport[2], viewport[3]);
    ::drnasDocumentPhase(line, "SNAPSHOT");
    const GLenum tokens[] = {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION};
    const char* names[] = {"VENDOR", "RENDERER", "VERSION", "GLSL"};
    for (int i = 0; i < 4; ++i) {
        ::drnasDocumentPhase(names[i], "GPU03_QT_QUERY_BEGIN");
        const GLubyte* value = functions->glGetString(tokens[i]);
        std::snprintf(line, sizeof(line), "GPU03 Qt %s=%s", names[i],
                      value ? reinterpret_cast<const char*>(value) : "(null)");
        ::drnasDocumentPhase(line, "SNAPSHOT");
        ::drnasDocumentPhase(names[i], "GPU04_LINKED_GL_QUERY_BEGIN");
        value = glGetString(tokens[i]);
        std::snprintf(line, sizeof(line), "GPU04 linked GL %s=%s", names[i],
                      value ? reinterpret_cast<const char*>(value) : "(null)");
        ::drnasDocumentPhase(line, "SNAPSHOT");
    }
    const void* addresses[] = {reinterpret_cast<const void*>(&glGetString),
        reinterpret_cast<const void*>(&glViewport), reinterpret_cast<const void*>(&glMatrixMode)};
    const char* symbols[] = {"glGetString", "glViewport", "glMatrixMode"};
    for (int i = 0; i < 3; ++i) {
        Dl_info info = {};
        const int found = dladdr(addresses[i], &info);
        std::snprintf(line, sizeof(line), "GPU05 %s=%p library=%s symbol=%s", symbols[i],
            addresses[i], found && info.dli_fname ? info.dli_fname : "(unknown)",
            found && info.dli_sname ? info.dli_sname : "(unknown)");
        ::drnasDocumentPhase(line, "SNAPSHOT");
    }
}
#endif
'''
    viewer_source = once(viewer_source, 'using namespace Gui;', 'using namespace Gui;\n' + gpu_probe)
    viewer_source = once(viewer_source, '    switch (renderType) {',
        '''#ifdef __ANDROID__
    if (_drnasTraceEnabled) drnasRenderGpuSnapshot(qobject_cast<QOpenGLWidget*>(this->viewport()));
#endif
    switch (renderType) {''')
    viewer_path.write_text(viewer_source)

    # Diagnostic APK: skip the crashing Coin3D draw, not document creation.
    # This intentionally leaves the 3D viewport blank while isolating P03.
    quarter_path = root / "src/Gui/Quarter/QuarterWidget.cpp"
    quarter_source = quarter_path.read_text()
    # Android EGL exposes GLES contexts. A desktop OpenGL compatibility
    # request cannot be satisfied there; legacy Coin calls still use GL4ES.
    quarter_source = once(quarter_source,
        '''        surfaceFormat.setRenderableType(QSurfaceFormat::OpenGL);
        surfaceFormat.setProfile(QSurfaceFormat::CompatibilityProfile);
        surfaceFormat.setOption(QSurfaceFormat::DeprecatedFunctions, true);''',
        '''#ifdef __ANDROID__
        surfaceFormat.setRenderableType(QSurfaceFormat::OpenGLES);
        surfaceFormat.setVersion(2, 0);
        surfaceFormat.setProfile(QSurfaceFormat::NoProfile);
        surfaceFormat.setOption(QSurfaceFormat::DeprecatedFunctions, false);
        surfaceFormat.setStereo(false);
        surfaceFormat.setSamples(0);
        surfaceFormat.setDepthBufferSize(24);
        surfaceFormat.setStencilBufferSize(8);
        ::drnasDocumentPhase("EGL01 request GLES=2.0 profile=none depth=24 stencil=8 samples=0 stereo=0", "BEFORE_SET_FORMAT");
#else
        surfaceFormat.setRenderableType(QSurfaceFormat::OpenGL);
        surfaceFormat.setProfile(QSurfaceFormat::CompatibilityProfile);
        surfaceFormat.setOption(QSurfaceFormat::DeprecatedFunctions, true);
#endif''')
    quarter_source = once(quarter_source,
        '        setFormat(surfaceFormat);',
        '''        setFormat(surfaceFormat);
#ifdef __ANDROID__
        myFormat = surfaceFormat; // Preserve GLES when recreating the viewport.
#endif''')
    quarter_source = once(quarter_source,
        'connect(this, &CustomGLWidget::resized, this, &CustomGLWidget::slotResized);',
        '''connect(this, &CustomGLWidget::resized, this, &CustomGLWidget::slotResized);
#ifdef __ANDROID__
        const QSurfaceFormat actual = this->format();
        char eglLine[384];
        std::snprintf(eglLine, sizeof(eglLine),
            "EGL02 actual valid=%d GLES=%d version=%d.%d profile=%d depth=%d stencil=%d samples=%d stereo=%d FBO=%u",
            int(this->isValid()), int(actual.renderableType() == QSurfaceFormat::OpenGLES),
            actual.majorVersion(), actual.minorVersion(), int(actual.profile()),
            actual.depthBufferSize(), actual.stencilBufferSize(), actual.samples(),
            int(actual.stereo()), this->defaultFramebufferObject());
        ::drnasDocumentPhase(eglLine, "INITIALIZE_GL");
#endif''')
    # Both variants use the same native library and genuine render path.
    # A leaves the desktop resize/DPR calculations unchanged. B derives Coin's
    # viewport from the actual QOpenGLWidget just before actualRedraw(), after
    # makeCurrent(), so outer QGraphicsView geometry cannot be mistaken for it.
    quarter_source = once(quarter_source,
        '    PRIVATE(this)->timesincelastframe.restart();', r'''#ifdef __ANDROID__
    const char* abMode = std::getenv("DRNAS_VIEWPORT_AB");
    const bool syncViewport = abMode && abMode[0] == 'B';
    const SbVec2s beforeSize = getSoRenderManager()->getViewportRegion().getViewportSizePixels();
    const qreal actualDpr = w->devicePixelRatioF();
    const int actualWidth = qRound(w->width() * actualDpr);
    const int actualHeight = qRound(w->height() * actualDpr);
    if (syncViewport && actualWidth > 0 && actualHeight > 0) {
        const SbViewportRegion actualViewport(actualWidth, actualHeight);
        if (int(beforeSize[0]) != actualWidth || int(beforeSize[1]) != actualHeight)
            PRIVATE(this)->sorendermanager->setViewportRegion(actualViewport);
        const SbVec2s eventSize = PRIVATE(this)->soeventmanager->getViewportRegion().getViewportSizePixels();
        if (int(eventSize[0]) != actualWidth || int(eventSize[1]) != actualHeight)
            PRIVATE(this)->soeventmanager->setViewportRegion(actualViewport);
    }
    static const void* lastWidget = nullptr;
    static int lastWidth = -1, lastHeight = -1, lastMode = -1;
    if (lastWidget != w || lastWidth != actualWidth || lastHeight != actualHeight || lastMode != int(syncViewport)) {
        char line[512];
        std::snprintf(line, sizeof(line),
            "VIEWPORT_AB mode=%c widget=%p outer=%dx%d glLogical=%dx%d dpr=%.4f expected=%dx%d coinBefore=%dx%d",
            syncViewport ? 'B' : 'A', static_cast<void*>(w), this->width(), this->height(),
            w->width(), w->height(), double(actualDpr), actualWidth, actualHeight,
            int(beforeSize[0]), int(beforeSize[1]));
        ::drnasDocumentPhase(line, "PRE_REDRAW");
        lastWidget = w; lastWidth = actualWidth; lastHeight = actualHeight; lastMode = int(syncViewport);
    }
#endif
    PRIVATE(this)->timesincelastframe.restart();''')
    # Opt-in experiment: retain the known-good blank-viewport fallback by default.
    # Set DRNAS_ENABLE_COIN_REDRAW=1 in the Android process before GUI startup
    # to test genuine Coin rendering; durable P03 markers bracket the attempt.
    quarter_source = once(quarter_source,
                          'this->actualRedraw();',
                          '''if (const char* enable = std::getenv("DRNAS_ENABLE_COIN_REDRAW");
        enable && enable[0] == '1' && enable[1] == '\\0') {
        const char* stage = std::getenv("DRNAS_RENDER_STAGE");
        char stageLabel[64];
        std::snprintf(stageLabel, sizeof(stageLabel), "RENDER_STAGE phase=%s", stage ? stage : "legacy");
        ::drnasDocumentPhase(stageLabel, "DRAW_BEGIN");
        ::drnasDocumentPhase("P03 Coin scene render", "REAL_REDRAW_BEGIN");
        const bool diagnosticColor = stage && stage[1] == 0 && (stage[0] == '6' || stage[0] == '7');
        const bool diagnosticPixel = stage && stage[1] == 0 && stage[0] == '8';
        // Stages 6/7 use the original Coin 3D widget, but NOT its scene graph.
        // Stage 8 measures the framebuffer after real Coin render.
        if (!diagnosticColor) this->actualRedraw();
        if (diagnosticColor || diagnosticPixel) {
            QOpenGLContext* current = QOpenGLContext::currentContext();
            bool valid = w && current && current->isValid() && current == w->context();
            QOpenGLFunctions* qt = valid ? current->functions() : nullptr;
            const GLuint qtFbo = w ? w->defaultFramebufferObject() : 0;
            const int pxWidth = w ? qRound(w->width() * w->devicePixelRatioF()) : 0;
            const int pxHeight = w ? qRound(w->height() * w->devicePixelRatioF()) : 0;
            valid = valid && qt && qtFbo && pxWidth > 0 && pxHeight > 0;
            int bridge = -3;
            GLint qtBound = -1, coinBound = -1;
            unsigned char sample[4] = {};
            GLenum qtError = GL_NO_ERROR, coinError = GL_NO_ERROR;
            if (valid) {
                qt->glGetError(); // Remove errors from prior frame for this diagnostic.
                glGetError();
                if (stage[0] == '6') {
                    using BindBridge = int (*)(unsigned int);
                    auto* bindBridge = reinterpret_cast<BindBridge>(
                        dlsym(RTLD_DEFAULT, "drnas_gl4es_bind_qt_default_fbo"));
                    bridge = bindBridge ? bindBridge(qtFbo) : -2;
                    glViewport(0, 0, pxWidth, pxHeight);
                    glClearColor(0.10f, 0.82f, 0.90f, 1.f); // CYAN: GL4ES
                    glClear(GL_COLOR_BUFFER_BIT);
                }
                if (stage[0] == '7') {
                    qt->glBindFramebuffer(GL_FRAMEBUFFER, qtFbo);
                    qt->glViewport(0, 0, pxWidth, pxHeight);
                    qt->glClearColor(0.12f, 0.88f, 0.18f, 1.f); // GREEN: GLES
                    qt->glClear(GL_COLOR_BUFFER_BIT);
                }
                qt->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &qtBound);
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &coinBound);
                qt->glReadPixels(pxWidth/2, pxHeight/2, 1, 1,
                                 GL_RGBA, GL_UNSIGNED_BYTE, sample);
                coinError = glGetError();
                qtError = qt->glGetError();
            }
            static char previousMode = 0;
            static unsigned int reported = 0;
            if (previousMode != stage[0]) { previousMode = stage[0]; reported = 0; }
            if (reported++ < 18 || !valid || qtError || coinError) {
                char line[512];
                std::snprintf(line, sizeof(line),
                    "GPU_PIXEL mode=%c valid=%d qtFbo=%u GLESbound=%d GL4ESbound=%d bridge=%d physical=%dx%d pixel=%u,%u,%u,%u glesError=0x%x gl4esError=0x%x",
                    stage[0], int(valid), qtFbo, int(qtBound), int(coinBound),
                    bridge, pxWidth, pxHeight, unsigned(sample[0]),
                    unsigned(sample[1]), unsigned(sample[2]), unsigned(sample[3]),
                    unsigned(qtError), unsigned(coinError));
                ::drnasDocumentPhase(line, valid && !qtError && !coinError ? "SAMPLE" : "WARNING");
            }
        }
        ::drnasDocumentPhase("P03 Coin scene render", "REAL_REDRAW_END");
        ::drnasDocumentPhase(stageLabel, "DRAW_END");
    } else {
        ::drnasDocumentPhase("P03 Coin scene render", "SKIPPED_ANDROID_DIAGNOSTIC");
    }''')
    quarter_source = '#include <dlfcn.h>\nextern "C" __attribute__((visibility("default"))) int drnas_gpu_color_version() { return 1; }\n' + quarter_source
    quarter_path.write_text(quarter_source)

    # Independent, session-only Coin stages for comparison with the GLES
    # BRep reference view. Unset preserves the legacy full-render probe.
    viewer_source = viewer_path.read_text()
    stage_helper = r'''
#ifdef __ANDROID__
extern "C" __attribute__((visibility("default")))
int drnas_render_stages_version() { return 1; }
static int drnasRenderStage()
{
    const char* stage = std::getenv("DRNAS_RENDER_STAGE");
    if (!stage) return 4;
    char* end = nullptr;
    const long value = std::strtol(stage, &end, 10);
    return end != stage && *end == '\0' && value >= 0 && value <= 10 ? int(value) : 4;
}
#else
static int drnasRenderStage() { return 4; }
#endif
'''
    viewer_source = once(viewer_source, gpu_probe, gpu_probe + stage_helper)
    start = viewer_source.index('void View3DInventorViewer::renderGLActionScene(')
    end = viewer_source.index('void View3DInventorViewer::renderScene()', start)
    body = viewer_source[start:end]
    body = body.replace('    if (!this->shading) {',
                '    if (drnasRenderStage() <= 1) return;\n\n    if (!this->shading) {', 1)
    body = once(body, 'inherited::actualRedraw();', r'''if (drnasRenderStage() == 2) {
            // Temporary graph; never change stored materials or ViewProviders.
            const CoinPtr<SoSeparator> basic(new SoSeparator);
            if (auto* camera = this->getSoRenderManager()->getCamera()) basic->addChild(camera);
            auto* material = new SoMaterial;
            material->diffuseColor.setValue(0.65f, 0.70f, 0.78f);
            material->transparency.setValue(0.0f);
            material->setOverride(true);
            basic->addChild(material);
            auto* lighting = new SoLightModel;
            lighting->model = SoLightModel::BASE_COLOR;
            lighting->setOverride(true);
            basic->addChild(lighting);
            basic->addChild(this->pcViewProviderRoot);
            ::drnasDocumentPhase("RENDER_STAGE basic geometry", "BEGIN");
            glra->apply(basic);
            ::drnasDocumentPhase("RENDER_STAGE basic geometry", "END");
        } else {
            inherited::actualRedraw();
        }''')
    for call in ('renderDelayedAnnotations(glra);',):
        body = once(body, call, 'if (drnasRenderStage() >= 3) ' + call)
    for call in ('glra->apply(this->foregroundroot);', 'glra->apply(this->decorationroot);'):
        body = once(body, call, 'if (drnasRenderStage() >= 4) ' + call)
    viewer_source = viewer_source[:start] + body + viewer_source[end:]
    start = viewer_source.index('void View3DInventorViewer::renderScene()')
    end = viewer_source.index('void View3DInventorViewer::renderRubberbandOverlay()', start)
    body = viewer_source[start:end]
    for call in ('printDimension();', 'it->paintGL();', 'renderRubberbandOverlay();'):
        body = once(body, call, 'if (drnasRenderStage() >= 4) ' + call)
    body = once(body,
        '    if (_drnasTraceEnabled)\n        ::drnasDocumentPhase("RS05A axis cross", "SKIPPED_ANDROID_ISOLATION");',
        r'''    const char* axes = std::getenv("DRNAS_RENDER_AXES");
    if (drnasRenderStage() >= 4 && axes && axes[0] == '1' && axes[1] == '\0') {
        ::drnasDocumentPhase("RENDER_STAGE axis cross", "BEGIN");
        this->drawAxisCross();
        ::drnasDocumentPhase("RENDER_STAGE axis cross", "END");
    } else if (_drnasTraceEnabled) {
        ::drnasDocumentPhase("RS05A axis cross", "SKIPPED_ANDROID_ISOLATION");
    }''')
    viewer_source = viewer_source[:start] + body + viewer_source[end:]
    viewer_path.write_text(viewer_source)

    # Read native GLES and GL4ES at the SAME render boundary. The older GPU02
    # sample precedes renderScene/glViewport and can contain a previous viewport.
    viewer_source = viewer_path.read_text()
    ab_probe = r'''
#ifdef __ANDROID__
extern "C" __attribute__((visibility("default")))
int drnas_viewport_ab_version() { return 1; }

static void drnasViewportAbSnapshot(const char* phase, QOpenGLWidget* widget,
                                   int coinWidth, int coinHeight)
{
    if (!widget) return;
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context || context != widget->context() || !context->isValid()) {
        ::drnasDocumentPhase("VIEWPORT_AB context unavailable", phase);
        return;
    }
    using Query = void (*)(unsigned int, int*);
    static void* nativeGles = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    static Query nativeQuery = nativeGles ? reinterpret_cast<Query>(dlsym(nativeGles, "glGetIntegerv")) : nullptr;
    int nativeVp[4] = {-1,-1,-1,-1}, nativeFbo = -1;
    if (nativeQuery) {
        nativeQuery(GL_VIEWPORT, nativeVp);
        nativeQuery(GL_FRAMEBUFFER_BINDING, &nativeFbo);
    }
    int wrappedVp[4] = {}, wrappedFbo = -1;
    glGetIntegerv(GL_VIEWPORT, wrappedVp);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &wrappedFbo);
    const char* mode = std::getenv("DRNAS_VIEWPORT_AB");
    char line[768];
    std::snprintf(line, sizeof(line),
        "VIEWPORT_AB mode=%c widget=%p coin=%dx%d glLogical=%dx%d dpr=%.4f QtFBO=%u nativeFBO=%d wrappedFBO=%d nativeVP=%d,%d,%d,%d wrappedVP=%d,%d,%d,%d",
        mode && mode[0]=='B' ? 'B' : 'A', static_cast<void*>(widget), coinWidth, coinHeight,
        widget->width(), widget->height(), double(widget->devicePixelRatioF()), widget->defaultFramebufferObject(),
        nativeFbo, wrappedFbo, nativeVp[0], nativeVp[1], nativeVp[2], nativeVp[3],
        wrappedVp[0], wrappedVp[1], wrappedVp[2], wrappedVp[3]);
    ::drnasDocumentPhase(line, phase);
}
#endif
'''
    # Insert after gpu_probe, whose declarations/includes this helper uses.

    fbo_sync = r'''
#ifdef __ANDROID__
static void drnasSyncQtCoinFbo(QOpenGLWidget* widget)
{
    const char* mode=std::getenv("DRNAS_COIN_FBO_SYNC");
    if (!mode || mode[0]!='1' || mode[1]!='\0' || !widget) return;
    auto* context=QOpenGLContext::currentContext();
    if (!context || !context->isValid() || context!=widget->context()) {
        ::drnasDocumentPhase("FBO_SYNC invalid or foreign context", "SKIP");
        return;
    }
    const GLuint qtFbo=widget->defaultFramebufferObject();
    if (!qtFbo) { ::drnasDocumentPhase("FBO_SYNC no Qt FBO", "SKIP"); return; }
    using Query=void (*)(unsigned int,int*);
    using BindQtFbo=int (*)(unsigned int);
    static void* gles=dlopen("libGLESv2.so",RTLD_NOW|RTLD_LOCAL);
    static void* gl4es=dlopen("libGL.so",RTLD_NOW|RTLD_LOCAL);
    static Query nativeQuery=gles ? reinterpret_cast<Query>(dlsym(gles,"glGetIntegerv")) : nullptr;
    static BindQtFbo bindQt=gl4es ? reinterpret_cast<BindQtFbo>(dlsym(gl4es,"drnas_gl4es_bind_qt_default_fbo")) : nullptr;
    int nativeBefore=-1,wrappedBefore=-1,nativeAfter=-1,wrappedAfter=-1;
    if(nativeQuery) nativeQuery(GL_FRAMEBUFFER_BINDING,&nativeBefore);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&wrappedBefore);
    // Qt's framebuffer is native id=qtFbo. GL4ES's DEFAULT framebuffer is
    // *logically* id=0 and maps to that Qt FBO, never id=qtFbo in its own map.
    const int bridge=bindQt ? bindQt(qtFbo) : -3;
    if(nativeQuery) nativeQuery(GL_FRAMEBUFFER_BINDING,&nativeAfter);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&wrappedAfter);
    const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    const GLenum error=glGetError();
    const bool passed=bridge==1 && nativeAfter==int(qtFbo) &&
        wrappedAfter==0 && status==GL_FRAMEBUFFER_COMPLETE && error==GL_NO_ERROR;
    static unsigned int samples=0;
    if(samples++<60 || !passed) {
        char line[480];
        std::snprintf(line,sizeof(line),
          "FBO_BRIDGE qt=%u nativeBefore=%d wrappedBefore=%d bridge=%d nativeAfter=%d wrappedDefault=%d status=0x%x error=0x%x logical=%dx%d physical=%dx%d dpr=%.4f",
          qtFbo,nativeBefore,wrappedBefore,bridge,nativeAfter,wrappedAfter,
          unsigned(status),unsigned(error),widget->width(),widget->height(),
          qRound(widget->width()*widget->devicePixelRatioF()),
          qRound(widget->height()*widget->devicePixelRatioF()),
          double(widget->devicePixelRatioF()));
        ::drnasDocumentPhase(line,passed?"PASS":"WARNING");
    }
}
#endif
'''
    geometry_probe = r'''
#ifdef __ANDROID__
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
extern "C" __attribute__((visibility("default")))
int drnas_geometry_probe_version() { return 1; }

static void drnasGeometryProbe(QOpenGLWidget* widget, int mode, unsigned int cacheContext)
{
    if (!widget || !QOpenGLContext::currentContext()) return;
    const int width=qRound(widget->width()*widget->devicePixelRatioF());
    const int height=qRound(widget->height()*widget->devicePixelRatioF());
    if (width<=0 || height<=0) return;
    using Query=void (*)(unsigned int,int*);
    using Read=void (*)(int,int,int,int,unsigned int,unsigned int,void*);
    using Error=unsigned int (*)();
    static void* gles=dlopen("libGLESv2.so",RTLD_NOW|RTLD_LOCAL);
    auto query=gles ? reinterpret_cast<Query>(dlsym(gles,"glGetIntegerv")) : nullptr;
    auto read=gles ? reinterpret_cast<Read>(dlsym(gles,"glReadPixels")) : nullptr;
    auto error=gles ? reinterpret_cast<Error>(dlsym(gles,"glGetError")) : nullptr;
    const GLenum prior=glGetError();
    if(error) for(int i=0;i<16 && error()!=GL_NO_ERROR;++i) {}
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glUseProgram(0);
    glViewport(0,0,width,height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glClearColor(0.03f,0.05f,0.10f,1.f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    const GLenum setupError=glGetError();
    ::drnasDocumentPhase(mode==9 ? "GEOMETRY_PROBE GL4ES triangle" : "GEOMETRY_PROBE isolated Coin cube", "BEGIN");
    if(mode==9) {
        glColor4f(1.f,0.3f,0.05f,1.f);
        glBegin(GL_TRIANGLES);
        glVertex3f(-0.75f,-0.65f,0.f);
        glVertex3f(0.75f,-0.65f,0.f);
        glVertex3f(0.f,0.75f,0.f);
        glEnd();
    } else {
        // Independent graph and camera: no FreeCAD ViewProvider or document.
        const CoinPtr<SoSeparator> root(new SoSeparator);
        root->renderCaching=SoSeparator::OFF;
        auto* camera=new SoOrthographicCamera;
        camera->position.setValue(3.f,2.f,5.f);
        camera->pointAt(SbVec3f(0.f,0.f,0.f));
        camera->height=4.5f;
        camera->nearDistance=0.1f; camera->farDistance=30.f;
        root->addChild(camera);
        auto* light=new SoLightModel; light->model=SoLightModel::BASE_COLOR;
        root->addChild(light);
        auto* material=new SoMaterial;
        material->diffuseColor.setValue(0.1f,0.95f,0.35f);
        root->addChild(material);
        root->addChild(new SoCube);
        const SbViewportRegion viewport(width,height);
        SoGLRenderAction action(viewport);
        action.setCacheContext(cacheContext);
        action.apply(root);
    }
    glFinish();
    const GLenum drawError=glGetError();
    int bound=-1; unsigned char center[4]={},corner[4]={};
    if(query) query(GL_FRAMEBUFFER_BINDING,&bound);
    if(read) {
        read(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,center);
        read(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,corner);
    }
    const unsigned int nativeError=error ? error() : 0;
    char line[640];
    std::snprintf(line,sizeof(line),
        "GEOMETRY_PROBE mode=%d qtFbo=%u nativeFbo=%d size=%dx%d prior=0x%x setup=0x%x draw=0x%x native=0x%x read=%d center=%u,%u,%u,%u corner=%u,%u,%u,%u",
        mode,widget->defaultFramebufferObject(),bound,width,height,unsigned(prior),unsigned(setupError),unsigned(drawError),nativeError,int(read!=nullptr),
        unsigned(center[0]),unsigned(center[1]),unsigned(center[2]),unsigned(center[3]),
        unsigned(corner[0]),unsigned(corner[1]),unsigned(corner[2]),unsigned(corner[3]));
    ::drnasDocumentPhase(line,"DRAW_RETURNED");
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glPopAttrib();
}
#endif
'''
    viewer_source = once(viewer_source, gpu_probe, gpu_probe + ab_probe + fbo_sync + geometry_probe)
    viewport_call = '    glViewport(origin[0], origin[1], size[0], size[1]);'
    viewer_source = once(viewer_source, viewport_call, r'''#ifdef __ANDROID__
    static unsigned int abFrames = 0;
    const bool traceViewport = abFrames++ < 32;
    auto* abWidget = qobject_cast<QOpenGLWidget*>(this->viewport());
    if (traceViewport) drnasViewportAbSnapshot("BEFORE_GL_VIEWPORT", abWidget, size[0], size[1]);
    drnasSyncQtCoinFbo(abWidget);
    if (drnasRenderStage()==9 || drnasRenderStage()==10) {
        drnasGeometryProbe(abWidget,drnasRenderStage(),
            this->getSoRenderManager()->getGLRenderAction()->getCacheContext());
        return;
    }
    if (traceViewport) drnasViewportAbSnapshot("AFTER_FBO_SYNC", abWidget, size[0], size[1]);
#endif
''' + viewport_call + r'''
#ifdef __ANDROID__
    if (traceViewport) drnasViewportAbSnapshot("AFTER_GL_VIEWPORT", abWidget, size[0], size[1]);
#endif''')
    viewer_path.write_text(viewer_source)


if __name__ == "__main__":
    patch(Path(sys.argv[1]))
    import subprocess
    subprocess.run([sys.executable, str(Path(__file__).with_name('test-service-registry-dso.py')),
                    sys.argv[1]], check=True)
    print("Render trace v7: staged Coin/reference lab, Android GLES EGL and A/B diagnostics")
