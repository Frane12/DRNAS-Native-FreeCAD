// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "NativeOcctViewport.h"
#include "StartupDiagnostics.h"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QOpenGLWidget>
#include <QMdiArea>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWindow>
#include <utility>
#include <functional>

// A child overlay in the original MDI area. No native window, reparenting of
// FreeCAD views, demo geometry, or startup GL allocation. Only one view is shown.
class DrnasRenderLab final : public QObject {
    QPointer<QWidget> upstream;
    QPointer<QWidget> panel;
    QPointer<QWidget> areaViewport;
    QPointer<NativeOcctViewport> reference;
    QPointer<QWidget> sheet;
    QPointer<QScrollArea> sheetScroll;
    QPointer<QLabel> status;
    std::function<bool(const QString&, QString&)> python;
    QTimer poll;
    QString directory;
    QByteArray previousDirty;
    QString renderedDocument;
    int stage=0;
    bool watching=false, busy=false;
    bool modelWire=false;
    void placeSheet() {
        if (!sheet || !areaViewport) return;
        const int w=qMin(380,qMax(1,areaViewport->width()-16));
        const int h=qMin(340,qMax(1,areaViewport->height()-16));
        sheetScroll->setGeometry((areaViewport->width()-w)/2,
                                (areaViewport->height()-h)/2,w,h);
        sheetScroll->raise();
    }
    QWidget* beginSheet(const QString& title) {
        if (!ensurePanel()) return nullptr;
        if (sheetScroll) { sheetScroll->hide(); sheetScroll->deleteLater(); }
        sheetScroll=new QScrollArea(panel);
        sheetScroll->setWidgetResizable(true);
        sheetScroll->setProperty("drnasDirectTouch",true);
        sheetScroll->setStyleSheet(QStringLiteral(
            "QScrollArea{background:#182d42;border:1px solid #52718d;}"
            "QLabel{color:white;padding:4px;}"
            "QPushButton,QComboBox,QDoubleSpinBox{min-height:36px;font-size:14px;}"));
        sheet=new QWidget;
        auto* layout=new QVBoxLayout(sheet);
        layout->addWidget(new QLabel(title,sheet));
        sheetScroll->setWidget(sheet);
        sheetScroll->show(); placeSheet();
        return sheet;
    }
    void closeSheet() {
        if (sheetScroll) { sheetScroll->hide(); sheetScroll->deleteLater(); }
        sheetScroll=nullptr; sheet=nullptr;
    }
    void sheetButtons(QWidget* body, std::function<void()> apply) {
        auto* row=new QHBoxLayout;
        auto* cancel=new QPushButton(QStringLiteral("Odustani"),body);
        auto* ok=new QPushButton(QStringLiteral("U redu"),body);
        row->addWidget(cancel); row->addWidget(ok);
        body->layout()->addItem(row);
        connect(cancel,&QPushButton::clicked,this,[this]{closeSheet();});
        connect(ok,&QPushButton::clicked,this,[apply]{apply();});
    }
    void resizeModel() {
        QWidget* body=beginSheet(QStringLiteral("Dimenzije kocke (mm)"));
        if (!body) return;
        auto* form=new QFormLayout;
        QList<QDoubleSpinBox*> fields;
        for (const auto& label:{QStringLiteral("Duljina"),QStringLiteral("Širina"),QStringLiteral("Visina")}) {
            auto* field=new QDoubleSpinBox(body);
            field->setRange(0.01,1000000); field->setDecimals(2); field->setValue(20);
            fields.append(field); form->addRow(label,field);
        }
        body->layout()->addItem(form);
        sheetButtons(body,[this,fields]{
            QJsonArray dimensions;
            for (auto* field:fields) dimensions.append(field->value());
            closeSheet();
            QJsonArray args; args.append(dimensions);
            modelCommand(QStringLiteral("resize"),args);
        });
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
        if (status) { status->show(); status->setText(QStringLiteral("Prikaz nije osvježen; vidi log")); }
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
        const bool ok=python(code,error);
        tick(true);
        if (!ok) {
            DrnasDiagnostics::note(QStringLiteral("MODEL_COMMAND ERROR ")+error);
            if (status) { status->show(); status->setText(error); }
        }
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
            if (status) { status->show(); status->setText(QStringLiteral("Dodaj tijelo u MODEL izborniku.")); }
            return;
        }
        QWidget* body=beginSheet(QStringLiteral("Odaberi FreeCAD objekt"));
        if (!body) return;
        auto* combo=new QComboBox(body); combo->addItems(choices);
        body->layout()->addWidget(combo);
        sheetButtons(body,[this,combo,names]{
            QJsonArray values; values.append(names.at(combo->currentIndex()));
            closeSheet(); modelCommand(QStringLiteral("select"),values);
        });
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
        status->setWordWrap(true);
        status->hide();
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
        if (sheetScroll) sheetScroll->raise();
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
            placeSheet();
        }
        return false;
    }
public:
    DrnasRenderLab(QWidget* owner,QWidget* cad,QToolBar* toolbar,
                   std::function<bool(const QString&,QString&)> bridge)
        :QObject(owner),upstream(cad),python(std::move(bridge)),
         directory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+QStringLiteral("/render-lab")) {
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
            this,[this]{resizeModel();});
        addModel(QStringLiteral("Rupa kroz odabrano tijelo"),QStringLiteral("cut"));
        addModel(QStringLiteral("Undo"),QStringLiteral("undo"));
        addModel(QStringLiteral("Redo"),QStringLiteral("redo"));
        QObject::connect(modelMenu->addAction(QStringLiteral("Fit našeg prikaza")),&QAction::triggered,
            this,[this]{setStage(0);if(reference)reference->fit();});
        QObject::connect(modelMenu->addAction(QStringLiteral("Bridovi")),&QAction::triggered,
            this,[this]{if(reference){modelWire=!modelWire;reference->setWire(modelWire);}});
        auto* viewMenu=new QMenu(toolbar);
        auto* viewButton=new QToolButton(toolbar);
        viewButton->setText(QStringLiteral("Prikaz"));
        viewButton->setMenu(viewMenu); viewButton->setPopupMode(QToolButton::InstantPopup);
        toolbar->addWidget(viewButton);
        connect(viewMenu->addAction(QStringLiteral("Fit")),&QAction::triggered,this,[this]{if(reference)reference->fit();});
        const QStringList views={QStringLiteral("Izometrija"),QStringLiteral("Sprijeda"),QStringLiteral("Odozgo"),QStringLiteral("Zdesna")};
        for (int i=0;i<views.size();++i)
            connect(viewMenu->addAction(views[i]),&QAction::triggered,this,[this,i]{if(reference)reference->setView(i);});
        auto* grid=viewMenu->addAction(QStringLiteral("Mreža")); grid->setCheckable(true); grid->setChecked(true);
        connect(grid,&QAction::toggled,this,[this](bool on){if(reference)reference->setGrid(on);});
        auto* refresh=viewMenu->addAction(QStringLiteral("Osvježi"));
        connect(refresh,&QAction::triggered,this,[this]{tick(true);});
        qputenv("DRNAS_ENABLE_COIN_REDRAW","0");
        qputenv("DRNAS_RENDER_STAGE","0");
        qputenv("DRNAS_COIN_FBO_SYNC","0");
        qputenv("DRNAS_RENDER_AXES","0");
        poll.setInterval(750);
        QObject::connect(&poll,&QTimer::timeout,this,[this]{tick();});
        QObject::connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{poll.stop();});
        // The toolbar is installed before Gui startup finishes. Delay allocation
        // of the reference GL widget until the original workspace is initialized.
        QTimer::singleShot(1500,this,[this]{poll.start();tick(true);});
        DrnasDiagnostics::note(QStringLiteral("DRNAS renderer integrated; Coin redraw disabled"));
    }
    void setStage(int value) {
        if (value!=0) return;
        stage=0;
        qputenv("DRNAS_ENABLE_COIN_REDRAW","0");
        tick(true);
    }
};
