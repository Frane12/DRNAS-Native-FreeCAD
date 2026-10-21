#include "NativeOcctViewport.h"
#include <algorithm>
#include <QFont>
#include <QSizePolicy>
#include <QAction>
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
