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
#include <QLabel>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QMdiArea>
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
    int stage=0;
    bool watching=false, busy=false, axes=false, nativeStages=false;
    QActionGroup* stages;
    QAction* axesAction;

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
        panel->setGeometry(areaViewport->rect());
        panel->show(); panel->raise();
        QFile dirty(directory+QStringLiteral("/dirty"));
        QByteArray revision;
        if (dirty.open(QIODevice::ReadOnly)) revision=dirty.readAll();
        if (force || revision!=previousDirty) {
            previousDirty=revision;
            DrnasDiagnostics::note(QStringLiteral("RENDER_BASELINE SNAPSHOT_BEGIN"));
            if (execute(QStringLiteral("_drnas_runtime._drnas_render_export(%1)\n").arg(argument()))) {
                QFile metadata(directory+QStringLiteral("/snapshot.json"));
                if (metadata.open(QIODevice::ReadOnly)) {
                    const auto info=QJsonDocument::fromJson(metadata.readAll()).object();
                    const int count=info.value(QStringLiteral("count")).toInt();
                    const QString name=info.value(QStringLiteral("document")).toString();
                    // Never display old geometry as if it belonged to a new document.
                    reference->clearShape();
                    if (count==0) status->setText(QStringLiteral("Naš prikaz — dokument nema vidljivih BRep tijela"));
                    else if (reference->loadBrep(directory+QStringLiteral("/document.brep"))) {
                        reference->fit();
                        status->setText(QStringLiteral("Naš prikaz · %1 · tijela: %2 · trokuti: %3")
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
        auto* refresh=toolbar->addAction(QStringLiteral("Osvježi naš prikaz"));
        QObject::connect(refresh,&QAction::triggered,this,[this]{setStage(0);tick(true);});
        axesAction=toolbar->addAction(QStringLiteral("Osi (test)"));
        axesAction->setCheckable(true); axesAction->setEnabled(false);
        axesAction->setToolTip(QStringLiteral("Zasebni test prethodno problematičnog križa osi; samo faza 4"));
        QObject::connect(axesAction,&QAction::triggered,this,[this](bool value){axes=value;setStage(stage);});
        qputenv("DRNAS_ENABLE_COIN_REDRAW","0");
        qputenv("DRNAS_RENDER_STAGE","0");
        qputenv("DRNAS_RENDER_AXES","0");
        poll.setInterval(750);
        QObject::connect(&poll,&QTimer::timeout,this,[this]{tick();});
        QObject::connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{poll.stop();});
        // The toolbar is installed before Gui startup finishes. Delay allocation
        // of the reference GL widget until the original workspace is initialized.
        QTimer::singleShot(1500,this,[this]{poll.start();tick(true);});
        DrnasDiagnostics::note(QStringLiteral("RENDER_LAB installed; native stages=%1; default=reference").arg(supported));
    }
    void setStage(int value) {
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
                if (stage!=requestedStage || stage<1 || stage>3) return;
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
    }
};
