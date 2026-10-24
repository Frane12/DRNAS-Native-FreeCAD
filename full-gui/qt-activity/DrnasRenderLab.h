// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "NativeOcctViewport.h"
#include "StartupDiagnostics.h"
#include <QActionGroup>
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QMessageBox>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>
#include <utility>
#include <dlfcn.h>
#include <functional>

// Independent Qt framebuffer probe. Deliberately does not call Coin/GL4ES:
// if this paints, Qt composition works and the fault is further upstream.
class DrnasFboProbe final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit DrnasFboProbe(QWidget* parent):QOpenGLWidget(parent) {
        setObjectName(QStringLiteral("drnasQtFboProbe"));
    }
protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        DrnasDiagnostics::note(QStringLiteral("FBO_PROBE INIT valid=%1 context=%2")
            .arg(isValid()).arg(QOpenGLContext::currentContext()!=nullptr));
    }
    void paintGL() override {
        GLint before=0, after=0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&before);
        const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glViewport(0,0,width()*devicePixelRatioF(),height()*devicePixelRatioF());
        glClearColor(0.82f,0.13f,0.65f,1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        const GLenum error=glGetError();
        unsigned char pixel[4]={0,0,0,0};
        glReadPixels(qMax(0,int(width()*devicePixelRatioF())/2),
                     qMax(0,int(height()*devicePixelRatioF())/2),
                     1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        const GLenum readError=glGetError();
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&after);
        static int samples=0;
        if (samples++<8) DrnasDiagnostics::note(QStringLiteral(
            "FBO_PROBE PAINT qtFbo=%1 boundBefore=%2 boundAfter=%3 status=0x%4 error=0x%5 readError=0x%6 pixel=%7,%8,%9,%10 logical=%11x%12 dpr=%13")
            .arg(defaultFramebufferObject()).arg(before).arg(after)
            .arg(uint(status),0,16).arg(uint(error),0,16)
            .arg(uint(readError),0,16).arg(pixel[0]).arg(pixel[1]).arg(pixel[2]).arg(pixel[3])
            .arg(width()).arg(height()).arg(devicePixelRatioF()));
    }
};

// A child overlay in the original MDI area. No native window, reparenting of
// FreeCAD views, demo geometry, or startup GL allocation. Only one view is shown.
class DrnasRenderLab final : public QObject {
    QPointer<QWidget> upstream;
    QPointer<QWidget> panel;
    QPointer<QWidget> areaViewport;
    QPointer<NativeOcctViewport> reference;
    QPointer<DrnasFboProbe> fboProbe;
    QPointer<QLabel> status;
    std::function<bool(const QString&, QString&)> python;
    QTimer poll;
    QString directory;
    QByteArray previousDirty;
    QString renderedDocument;
    int stage=0;
    bool watching=false, busy=false, axes=false, nativeStages=false;
    QActionGroup* stages;
    QAction* axesAction;
    int coinWindowChecks=0;
    bool modelWire=false;
    void inspectCoinWindow(const QString& phase) {
        if (!upstream) return;
        auto* mdi=upstream->findChild<QMdiArea*>();
        if (!mdi) { DrnasDiagnostics::note(QStringLiteral("COIN_WINDOW %1 no MDI area").arg(phase)); return; }
        const auto windows=mdi->subWindowList();
        DrnasDiagnostics::note(QStringLiteral("COIN_WINDOW %1 mdi=%2x%3 subwindows=%4 active=%5")
            .arg(phase).arg(mdi->width()).arg(mdi->height()).arg(windows.size())
            .arg(mdi->activeSubWindow()!=nullptr));
        for (auto* window:windows) {
            const auto glViews=window->findChildren<QOpenGLWidget*>();
            DrnasDiagnostics::note(QStringLiteral("COIN_WINDOW candidate title=%1 visible=%2 glViews=%3 size=%4x%5")
                .arg(window->windowTitle()).arg(window->isVisible()).arg(glViews.size())
                .arg(window->width()).arg(window->height()));
            if (glViews.isEmpty()) continue;
            if (phase==QStringLiteral("foreground")) {
                // Original FreeCAD document subwindow, NOT a duplicated/reparented GL widget.
                // Keeping it inside the existing Android QWindow avoids the known Qt
                // 'touch event received for non-existing window' lifecycle failure.
                window->show();
                if (!window->isMaximized()) window->showMaximized();
                mdi->setActiveSubWindow(window);
                window->raise();
                window->setFocus(Qt::OtherFocusReason);
                for (auto* gl:glViews) {
                    DrnasDiagnostics::note(QStringLiteral("COIN_WINDOW GL valid=%1 visible=%2 size=%3x%4 fbo=%5")
                        .arg(gl->isValid()).arg(gl->isVisible()).arg(gl->width()).arg(gl->height())
                        .arg(gl->defaultFramebufferObject()));
                    gl->update();
                }
                return;
            }
        }
        if (phase==QStringLiteral("foreground"))
            DrnasDiagnostics::note(QStringLiteral("COIN_WINDOW WARNING: no original Coin GL document subwindow; create/open a document first"));
    }
    void auditWarnings() {
        DrnasDiagnostics::note(QStringLiteral("WARN_AUDIT BEGIN Std_Measure Std_MassProperties"));
        execute(QStringLiteral(
            "import FreeCADGui as Gui\n"
            "try:\n"
            "    _drnas_known_commands = set(Gui.listCommands())\n"
            "    for _name in ('Std_Measure', 'Std_MassProperties'):\n"
            "        print('DRNAS WARN_AUDIT', _name, 'registered=', _name in _drnas_known_commands, flush=True)\n"
            "except Exception as _e:\n"
            "    print('DRNAS WARN_AUDIT failure', repr(_e), flush=True)\n"));
        DrnasDiagnostics::note(QStringLiteral("WARN_AUDIT END (Python script executed; inspect command registration output)"));
    }
    QString argument() const {
        QJsonArray a; a.append(directory);
        const QByteArray json=QJsonDocument(a).toJson(QJsonDocument::Compact);
        return QString::fromUtf8(json.mid(1,json.size()-2));
    }
    bool execute(const QString& script) {
        QString error;
        if (python(script,error)) return true;
        DrnasDiagnostics::note(QStringLiteral("RENDER_LAB ERROR: ")+error);
        if (status) status->setText(QStringLiteral("Prikaz nije osvježen; vidi log"));
        return false;
    }
    void modelCommand(const QString& action, const QJsonArray& values=QJsonArray()) {
        setStage(0);
        QJsonArray args; args.append(action);
        for (const auto& value:values) args.append(value);
        QString error;
        const QString code=QStringLiteral(
            "import drnas_android_runtime as _drnas_runtime\n"
            "_drnas_runtime._drnas_model_command(*%1)\n")
            .arg(QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
        if (!python(code,error)) {
            DrnasDiagnostics::note(QStringLiteral("MODEL_COMMAND ERROR ")+error);
            QMessageBox::warning(upstream,QStringLiteral("MODEL LAB"),error);
        }
        tick(true);
    }
    void selectModelObject() {
        tick(true);
        QFile file(directory+QStringLiteral("/snapshot.json"));
        if (!file.open(QIODevice::ReadOnly)) return;
        const auto info=QJsonDocument::fromJson(file.readAll()).object();
        const auto names=info.value(QStringLiteral("objects")).toArray();
        const auto labels=info.value(QStringLiteral("labels")).toArray();
        QStringList choices;
        for (int i=0;i<names.size();++i)
            choices.append(QStringLiteral("%1 [%2]").arg(labels.at(i).toString(),names.at(i).toString()));
        if (choices.isEmpty()) {
            QMessageBox::information(upstream,QStringLiteral("MODEL LAB"),QStringLiteral("Nema vidljivih BRep objekata. Dodaj kocku."));
            return;
        }
        bool ok=false;
        const QString chosen=QInputDialog::getItem(upstream,QStringLiteral("Odaberi objekt"),
            QStringLiteral("FreeCAD objekt"),choices,0,false,&ok);
        if (ok) { QJsonArray values; values.append(names.at(choices.indexOf(chosen)));
            modelCommand(QStringLiteral("select"),values); }
    }
    bool ensurePanel() {
        if (panel) return true;
        if (!upstream) return false;
        auto* mdi=upstream->findChild<QMdiArea*>();
        if (!mdi) return false;
        areaViewport=mdi->viewport();
        panel=new QWidget(areaViewport);
        panel->setObjectName(QStringLiteral("drnasReferenceRenderer"));
        panel->setAutoFillBackground(true);
        auto* layout=new QVBoxLayout(panel);
        layout->setContentsMargins(0,0,0,0);
        status=new QLabel(QStringLiteral("Naš prikaz — čeka FreeCAD geometriju"),panel);
        status->setStyleSheet(QStringLiteral("background:#182d42;color:white;padding:5px;"));
        reference=new NativeOcctViewport(panel,false);
        layout->addWidget(status);
        layout->addWidget(reference,1);
        areaViewport->installEventFilter(this);
        panel->setGeometry(areaViewport->rect());
        return true;
    }
    void tick(bool force=false) {
        if (busy || stage!=0 || !upstream || !upstream->updatesEnabled() ||
            QGuiApplication::applicationState()!=Qt::ApplicationActive ||
            !upstream->window()->windowHandle() ||
            !upstream->window()->windowHandle()->isExposed()) return;
        busy=true;
        if (!watching) watching=execute(QStringLiteral(
            "import drnas_android_runtime as _drnas_runtime\n"
            "_drnas_runtime._drnas_render_watch(%1)\n").arg(argument()));
        if (!watching || !ensurePanel()) { busy=false; return; }
        execute(QStringLiteral("_drnas_runtime._drnas_render_poll()\n"));
        panel->setGeometry(areaViewport->rect());
        panel->show(); panel->raise();
        QFile dirty(directory+QStringLiteral("/dirty"));
        QByteArray revision;
        if (dirty.open(QIODevice::ReadOnly)) revision=dirty.readAll();
        if (force || revision!=previousDirty) {
            DrnasDiagnostics::note(QStringLiteral("RENDER_BASELINE SNAPSHOT_BEGIN"));
            if (execute(QStringLiteral("_drnas_runtime._drnas_render_export(%1)\n").arg(argument()))) {
                QFile metadata(directory+QStringLiteral("/snapshot.json"));
                if (metadata.open(QIODevice::ReadOnly)) {
                    const auto info=QJsonDocument::fromJson(metadata.readAll()).object();
                    const int count=info.value(QStringLiteral("count")).toInt();
                    const QString name=info.value(QStringLiteral("document")).toString();
                    previousDirty=revision;
                    // Never display old geometry as if it belonged to a new document.
                    reference->clearShape();
                    if (count==0) status->setText(QStringLiteral("Naš prikaz — dokument nema vidljivih BRep tijela"));
                    else if (reference->loadBrep(directory+QStringLiteral("/document.brep"))) {
                        if (renderedDocument!=name) reference->fit();
                        renderedDocument=name;
                        status->setText(QStringLiteral("MODEL LAB · %1 · tijela: %2 · trokuti: %3")
                            .arg(name).arg(count).arg(reference->triangleCount()));
                    } else status->setText(QStringLiteral("BRep se nije mogao prikazati; vidi log"));
                }
            } else reference->clearShape();
            DrnasDiagnostics::note(QStringLiteral("RENDER_BASELINE SNAPSHOT_END"));
        }
        busy=false;
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if (object==areaViewport && event->type()==QEvent::Resize) {
            if (panel) panel->setGeometry(areaViewport->rect());
            if (fboProbe) fboProbe->setGeometry(areaViewport->rect());
        }
        return false;
    }
public:
    DrnasRenderLab(QWidget* owner,QWidget* cad,QToolBar* toolbar,
                   std::function<bool(const QString&,QString&)> bridge)
        :QObject(owner),upstream(cad),python(std::move(bridge)),
         directory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+QStringLiteral("/render-lab")) {
        // Compact popup keeps all GPU trials reachable on a narrow phone.
        auto* gpuMenu = new QMenu(toolbar);
        auto* gpuButton = new QToolButton(toolbar);
        gpuButton->setText(QStringLiteral("GPU TEST"));
        gpuButton->setObjectName(QStringLiteral("drnasGpuExperiments"));
        gpuButton->setMenu(gpuMenu);
        gpuButton->setPopupMode(QToolButton::InstantPopup);
        toolbar->addWidget(gpuButton);
        toolbar->addSeparator();
        // Two one-tap renderer controls, placed BEFORE the diagnostic stage actions.
        // They remain directly accessible without opening the stage menu.
        auto* drnasButton=toolbar->addAction(QStringLiteral("DRNAS RENDER"));
        drnasButton->setObjectName(QStringLiteral("drnasOneTapReferenceRenderer"));
        drnasButton->setToolTip(QStringLiteral("Show the working DRNAS diagnostic BRep renderer"));
        QObject::connect(drnasButton,&QAction::triggered,this,[this] {
            DrnasDiagnostics::note(QStringLiteral("ONE_TAP DRNAS_RENDER requested"));
            setStage(0);
        });
        auto* coinButton=toolbar->addAction(QStringLiteral("COIN3D FULL"));
        coinButton->setObjectName(QStringLiteral("drnasOneTapCoin3D"));
        coinButton->setToolTip(QStringLiteral("Enable genuine FreeCAD Coin3D scene and appearance; known crashing NaviCube remains isolated"));
        QObject::connect(coinButton,&QAction::triggered,this,[this] {
            DrnasDiagnostics::note(QStringLiteral("ONE_TAP COIN3D_FULL requested (stage 3; NaviCube crash guard retained)"));
            setStage(3);
            QTimer::singleShot(350,this,[this]{inspectCoinWindow(QStringLiteral("foreground"));});
        });
        toolbar->addSeparator();
        auto* modelMenu=new QMenu(toolbar);
        auto* modelButton=new QToolButton(toolbar);
        modelButton->setText(QStringLiteral("MODEL"));
        modelButton->setObjectName(QStringLiteral("drnasModelCommands"));
        modelButton->setMenu(modelMenu);
        modelButton->setPopupMode(QToolButton::InstantPopup);
        toolbar->addWidget(modelButton);
        auto addModel=[this,modelMenu](const QString& title,const QString& command) {
            auto* action=modelMenu->addAction(title);
            QObject::connect(action,&QAction::triggered,this,[this,command]{modelCommand(command);});
        };
        addModel(QStringLiteral("Dodaj kocku 20 mm"),QStringLiteral("box"));
        QObject::connect(modelMenu->addAction(QStringLiteral("Odaberi objekt")),&QAction::triggered,
            this,[this]{selectModelObject();});
        QObject::connect(modelMenu->addAction(QStringLiteral("Dimenzije odabrane kocke")),&QAction::triggered,
            this,[this]{
                QJsonArray dimensions;
                for (const auto& label: {QStringLiteral("Duljina"),QStringLiteral("Širina"),QStringLiteral("Visina")}) {
                    bool ok=false;
                    const double value=QInputDialog::getDouble(upstream,QStringLiteral("Dimenzije kocke"),
                        label+QStringLiteral(" (mm)"),20,0.01,1000000,2,&ok);
                    if (!ok) return;
                    dimensions.append(value);
                }
                QJsonArray args; args.append(dimensions); modelCommand(QStringLiteral("resize"),args);
            });
        addModel(QStringLiteral("Rupa kroz odabrano tijelo"),QStringLiteral("cut"));
        addModel(QStringLiteral("Undo"),QStringLiteral("undo"));
        addModel(QStringLiteral("Redo"),QStringLiteral("redo"));
        QObject::connect(modelMenu->addAction(QStringLiteral("Fit našeg prikaza")),&QAction::triggered,
            this,[this]{setStage(0);if(reference)reference->fit();});
        QObject::connect(modelMenu->addAction(QStringLiteral("Bridovi")),&QAction::triggered,
            this,[this]{if(reference){modelWire=!modelWire;reference->setWire(modelWire);}});
        stages=new QActionGroup(this);
        const bool supported=dlsym(RTLD_DEFAULT,"drnas_render_stages_version")!=nullptr;
        nativeStages=supported;
        const QStringList labels={QStringLiteral("0 Naš prikaz"),QStringLiteral("1 Coin pozadina"),
            QStringLiteral("2 Coin tijelo"),QStringLiteral("3 Coin izgled"),QStringLiteral("4 Coin dodaci"),
            QStringLiteral("5 Qt FBO test")};
        for (int i=0;i<labels.size();++i) {
            auto* action=toolbar->addAction(labels[i]);
            action->setObjectName(QStringLiteral("drnasRenderStage%1").arg(i));
            action->setCheckable(true); action->setData(i); stages->addAction(action);
            action->setEnabled(i==0 || i==5 || supported);
            if (i==0) action->setChecked(true);
            QObject::connect(action,&QAction::triggered,this,[this,i]{setStage(i);});
        }
        const bool gpuReady = dlsym(RTLD_DEFAULT, "drnas_gpu_color_version") != nullptr;
        const QStringList gpuModes = {
            QStringLiteral("6 GL4ES cijan (Qt most)"),
            QStringLiteral("7 GLES zelen (native)"),
            QStringLiteral("8 Coin3D (pixel readback)")
        };
        for (int i=6; i<=8; ++i) {
            auto* action = gpuMenu->addAction(gpuModes[i-6]);
            action->setCheckable(true);
            action->setData(i);
            action->setObjectName(QStringLiteral("drnasGpuProbe%1").arg(i));
            action->setEnabled(gpuReady);
            stages->addAction(action);
            QObject::connect(action,&QAction::triggered,this,[this,i]{setStage(i);});
        }
        DrnasDiagnostics::note(QStringLiteral("GPU_TEST installed native=%1 modes=6,7,8").arg(gpuReady));
        auto* refresh=toolbar->addAction(QStringLiteral("Osvježi naš prikaz"));
        QObject::connect(refresh,&QAction::triggered,this,[this]{setStage(0);tick(true);});
        axesAction=toolbar->addAction(QStringLiteral("Osi (test)"));
        axesAction->setCheckable(true); axesAction->setEnabled(false);
        axesAction->setToolTip(QStringLiteral("Zasebni test prethodno problematičnog križa osi; samo faza 4"));
        QObject::connect(axesAction,&QAction::triggered,this,[this](bool value){axes=value;setStage(stage);});
        qputenv("DRNAS_ENABLE_COIN_REDRAW","0");
        qputenv("DRNAS_RENDER_STAGE","0");
        qputenv("DRNAS_COIN_FBO_SYNC","0");
        qputenv("DRNAS_RENDER_AXES","0");
        coinButton->setVisible(false);
        gpuButton->setVisible(false);
        axesAction->setVisible(false);
        for (auto* action:stages->actions()) action->setVisible(false);
        poll.setInterval(750);
        QTimer::singleShot(2400,this,[this]{auditWarnings();inspectCoinWindow(QStringLiteral("startup"));});
        QObject::connect(&poll,&QTimer::timeout,this,[this]{tick();});
        QObject::connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{poll.stop();});
        // The toolbar is installed before Gui startup finishes. Delay allocation
        // of the reference GL widget until the original workspace is initialized.
        QTimer::singleShot(1500,this,[this]{poll.start();tick(true);});
        DrnasDiagnostics::note(QStringLiteral("RENDER_LAB installed; native stages=%1; default=reference").arg(supported));
    }
    void setStage(int value) {
        // This branch exercises FreeCAD modeling through the native GLES BRep view.
        if (value!=0) return;
        if (value==4) {
            DrnasDiagnostics::note(QStringLiteral("RENDER_STAGE GUARD: stage=4 NaviCube blocked after confirmed segfault"));
            return;
        }
        stage=value;
        if (stage!=4) axes=false;
        axesAction->setEnabled(false);
        for (auto* action:stages->actions()) action->setChecked(action->data().toInt()==stage);
        axesAction->setChecked(axes);
        qputenv("DRNAS_RENDER_STAGE",QByteArray::number(stage));
        qputenv("DRNAS_RENDER_AXES",axes && stage==4 ? "1" : "0");
        qputenv("DRNAS_ENABLE_COIN_REDRAW",(stage==0 || stage==5) ? "0" : "1");
        qputenv("DRNAS_COIN_FBO_SYNC",((stage>=1 && stage<=3) || stage==8) ? "1" : "0");
        if (fboProbe) fboProbe->hide();
        DrnasDiagnostics::note(QStringLiteral("RENDER_STAGE SELECT stage=%1 axes=%2 BEGIN").arg(stage).arg(axes));
        if (stage==0) tick(true);
        else if (stage==5) {
            if (panel) panel->hide();
            if (ensurePanel() && areaViewport) {
                if (!fboProbe) fboProbe=new DrnasFboProbe(areaViewport);
                fboProbe->setGeometry(areaViewport->rect());
                fboProbe->show(); fboProbe->raise(); fboProbe->update();
                DrnasDiagnostics::note(QStringLiteral("FBO_PROBE ACTIVE: magenta expected; Coin disabled"));
            }
        } else {
            if (panel) panel->hide();
            execute(QStringLiteral(
                "import FreeCAD as App, FreeCADGui as Gui\n"
                "App.ParamGet('User parameter:BaseApp/Preferences/View').SetBool('ShowNaviCube', %1)\n"
                "if Gui.activeDocument() is not None:\n"
                "    Gui.activeDocument().activeView().setAxisCross(%2)\n"
                "    Gui.activeDocument().activeView().redraw()\n").arg(QStringLiteral("False"),
                    axes && stage==4 ? QStringLiteral("True") : QStringLiteral("False")));
            // Coin/Quarter repaint A/B: a second redraw after Qt has finished
            // dismissing the Tools popup. This does not modify the OCCT renderer.
            const int requestedStage=stage;
            QTimer::singleShot(180,this,[this,requestedStage] {
                if (stage!=requestedStage || stage<1 || stage>8 || stage==5) return;
                if (upstream) {
                    const auto views=upstream->findChildren<QOpenGLWidget*>();
                    DrnasDiagnostics::note(QStringLiteral("COIN_REPAINT_AB stage=%1 views=%2 activePopup=%3")
                        .arg(stage).arg(views.size())
                        .arg(QApplication::activePopupWidget()!=nullptr));
                    for (auto* view:views) {
                        if (view && view->isVisible()) view->update();
                    }
                }
                execute(QStringLiteral(
                    "import FreeCADGui as Gui\n"
                    "if Gui.activeDocument() is not None:\n"
                    "    Gui.activeDocument().activeView().redraw()\n"));
            });
        }
        DrnasDiagnostics::note(QStringLiteral("RENDER_STAGE SELECT stage=%1 QUEUED").arg(stage));
        if ((stage>=1 && stage<=3) || stage>=6)
            QTimer::singleShot(450,this,[this,value]{if(stage==value) inspectCoinWindow(QStringLiteral("foreground"));});
    }
};
