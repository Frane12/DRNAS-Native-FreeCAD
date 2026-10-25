// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../DrnasTouchInput.h"
#include "../DrnasPortraitLayout.h"
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QScrollBar>
#include <QTest>
#include <QWindow>
#include <iostream>
namespace DrnasDiagnostics { void note(const QString& s) { qWarning().noquote()<<s; } }
namespace Gui {
// Match the upstream runtime type without linking FreeCAD into desktop tests.
class OverlayTabWidget : public QTabWidget {
    Q_OBJECT
public:
    using QTabWidget::QTabWidget;
};
}
void check(bool ok,const char* message) { if (!ok) qFatal("%s",message); }
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QMainWindow host,cad;
    host.resize(400,869);host.setCentralWidget(&cad);
    auto* scene=new QWidget(&cad);cad.setCentralWidget(scene);
    QMap<QString,QWidget*> originalPanels;
    for (const char* name:{"Model","Property view","Tasks"}) {
        auto* dock=new QDockWidget(QString::fromLatin1(name),&cad);
        dock->setObjectName(QString::fromLatin1(name));
        auto* content=new QWidget(dock);
        auto* contentLayout=new QVBoxLayout(content);
        auto* scroll=new QScrollArea(content);scroll->setWidgetResizable(true);
        auto* form=new QLabel("Original CAD panel",scroll);
        form->setMinimumWidth(600); // Stock task forms can exceed phone width.
        scroll->setWidget(form);contentLayout->addWidget(scroll);
        content->setMinimumWidth(515);
        originalPanels.insert(QString::fromLatin1(name),content);
        dock->setWidget(content);
        auto* title=new QLabel("Desktop overlay controls",dock);
        title->setObjectName("OverlayTitle");title->setMinimumWidth(515);
        dock->setTitleBarWidget(title);
        cad.addDockWidget(Qt::LeftDockWidgetArea,dock);
    }
    auto* bar=new QToolBar(&cad);cad.addToolBar(bar);
    auto* action=bar->addAction("New");int actions=0;
    action->setShortcut(QKeySequence("Ctrl+N"));
    QObject::connect(action,&QAction::triggered,[&]{++actions;});
    for (int i=0;i<2;++i) {
        auto* extraBar=new QToolBar(&cad);cad.addToolBar(extraBar);
        auto* selector=new QComboBox(extraBar);selector->addItem("Workbench selector");
        selector->setMinimumWidth(210);extraBar->addWidget(selector);
    }
    auto* input=new DrnasTouchInput(&host,&cad,[](const QString&){
        qFatal("UI/dialog taps must never call Python or camera commands");
    });
    Q_UNUSED(input);
    auto* restoredOverlay=new Gui::OverlayTabWidget(scene);
    restoredOverlay->addTab(new QWidget(restoredOverlay),"Restored overlay");
    restoredOverlay->hide();
    int overlayDisables=0;
    new DrnasPortraitLayout(&host,&cad,[&]{
        ++overlayDisables;
        while (restoredOverlay->count()) restoredOverlay->removeTab(0);
    });
    host.show();QTest::qWait(150);
    auto* device=QTest::createTouchDevice();
    auto tap=[&](QWidget* receiver,QWidget* button,int id) {
        // Real Android input enters the native QtActivity QWindow. Embedded
        // widgets have no windowHandle; don't let QTest guess a screen/window.
        QWindow* window=receiver->window()->windowHandle();
        check(window,"touch must enter an existing native surface");
        const QPoint p=window->mapFromGlobal(button->mapToGlobal(button->rect().center()));
        QTest::touchEvent(window,device).press(id,p,window).commit();
        QTest::touchEvent(window,device).release(id,p,window).commit();
        QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();
    };
    check(host.size()==QSize(400,869),"desktop panel minimums must not enlarge the phone host");
    for (auto* dock:cad.findChildren<QDockWidget*>()) {
        check(cad.dockWidgetArea(dock)==Qt::BottomDockWidgetArea,"portrait panels must be below the scene");
        check(!(dock->features() & (QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable)),
              "phone panel tabs must not expose desktop drag/floating controls");
        check(dock->titleBarWidget()->objectName()=="drnasPhoneDockTitle",
              "upstream overlay title bars must be replaced on ordinary docks too");
        check(dock->widget()==originalPanels.value(dock->objectName()),
              "DockWindowManager must still return the original TaskView/ComboView object");
        auto* scroll=dock->widget()->findChild<QScrollArea*>();
        check(scroll && scroll->widget()->minimumWidth()==600,
              "wide original task controls must remain intact in a scroll area");
        dock->raise();QTest::qWait(100);
        check(scroll->horizontalScrollBar()->maximum()>0,"wide panel content must be reachable by scrolling");
        check(host.size()==QSize(400,869),"switching panel tabs must preserve phone geometry");
    }
    auto* lateDock=new QDockWidget("Selection view",&cad);
    lateDock->setObjectName("Selection view");lateDock->setWidget(new QLabel("Late workbench panel",lateDock));
    cad.addDockWidget(Qt::RightDockWidgetArea,lateDock);lateDock->show();QTest::qWait(200);
    check(cad.dockWidgetArea(lateDock)==Qt::BottomDockWidgetArea,
          "panels created by later workbenches must join the bottom tabs");
    check(host.size()==QSize(400,869),"late workbench panels must not grow the native window");
    check(overlayDisables==1,"only populated upstream overlays may trigger ToggleAll, exactly once");
    for (auto* toolbar:cad.findChildren<QToolBar*>())
        check(toolbar->geometry().right()<cad.width(),"toolbar rows must fit inside the phone width");
    check(scene->width()>350 && scene->height()>400,"portrait scene must retain usable width and height");
    auto* newButton=qobject_cast<QToolButton*>(bar->widgetForAction(action));
    check(newButton,"New toolbar action must have a button");
    for (int i=0;i<3;++i) {
        QDialog box(&cad);box.setModal(true);
        auto* layout=new QVBoxLayout(&box);
        layout->addWidget(new QLabel("Spremiti promjene prije zatvaranja?",&box));
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Discard|QDialogButtonBox::Cancel,&box);
        layout->addWidget(buttons);
        QObject::connect(buttons,&QDialogButtonBox::clicked,&box,[&](QAbstractButton* b){
            box.done(buttons->standardButton(b));
        });
        const auto choice=i==0 ? QDialogButtonBox::Cancel : i==1 ? QDialogButtonBox::Discard : QDialogButtonBox::Save;
        QTimer::singleShot(50,&box,[&]{
            check(!box.isWindow() && box.windowHandle()==nullptr && box.property("drnasEmbeddedModal").toBool(),
                  "modal exec must stay on the QtActivity surface");
            QTest::keyClick(&box,Qt::Key_N,Qt::ControlModifier);
            tap(&host,newButton,40+i); // Must not leak through the modal.
            check(actions==i,"modal must block the underlying New command");
            // Second iteration simulates Android delivering to QtActivity.
            tap(i==1 ? static_cast<QWidget*>(&host) : &box,buttons->button(choice),50+i);
        });
        QTimer::singleShot(1500,&box,&QDialog::reject); // Fail, never hang CI.
        check(box.exec()==choice,"finger tap must activate the requested modal button");
        QTest::qWait(30);
        tap(&host,newButton,60+i);
        check(actions==i+1,"toolbar touch must work again after the modal closes");
    }
    // Real QMessageBox used by FreeCAD's document-close prompt.
    QMessageBox save(QMessageBox::Question,"FreeCAD","Sačuvajte sve promjene na dokumentu 'Neimenovano' prije zatvaranja?",
                     QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,&cad);
    save.setInformativeText("U suprotnom, sve promjene će biti izgubljene.");
    QTimer::singleShot(50,&save,[&]{
        for (auto* button:save.findChildren<QAbstractButton*>()) {
            const QRect r(button->mapToGlobal(QPoint()),button->size());
            check(QRect(host.mapToGlobal(QPoint()),host.size()).contains(r),"every save prompt button must fit the phone screen");
        }
        tap(&save,save.button(QMessageBox::Cancel),70);
    });
    QTimer::singleShot(1500,&save,&QDialog::reject);
    check(save.exec()==QMessageBox::Cancel,"stock FreeCAD save prompt must accept finger Cancel");
    QTest::qWait(30);tap(&host,newButton,71);
    check(actions==4,"closing the stock save prompt must preserve toolbar touch");
    // Popup actions use the same logical-coordinate route.
    QMenu menu(&host);auto* item=menu.addAction("Menu action");int menuActions=0;
    QObject::connect(item,&QAction::triggered,[&]{++menuActions;});
    menu.popup(host.mapToGlobal(QPoint(50,100)));QTest::qWait(20);
    const QPoint p=menu.actionGeometry(item).center();
    QTest::touchEvent(&menu,device).press(72,p,&menu).commit();
    QTest::touchEvent(&menu,device).release(72,p,&menu).commit();
    QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();
    check(menuActions==1 && !menu.isVisible(),"finger menu tap must activate once and release the popup");
    QDialog outer(&cad);auto* outerLayout=new QVBoxLayout(&outer);
    auto* confirm=new QPushButton("Confirm",&outer);outerLayout->addWidget(confirm);
    bool nestedAccepted=false;
    QObject::connect(confirm,&QPushButton::clicked,&outer,[&]{
        QMessageBox inner(QMessageBox::Question,"Confirm","Nested prompt",QMessageBox::Ok,&outer);
        QTimer::singleShot(50,&inner,[&]{tap(&host,inner.button(QMessageBox::Ok),81);});
        QTimer::singleShot(1500,&inner,&QDialog::reject);
        nestedAccepted=inner.exec()==QMessageBox::Ok;
        check(host.property("drnasEmbeddedModalActive").toBool(),"closing an inner dialog must preserve outer modality");
        outer.accept();
    });
    QTimer::singleShot(50,&outer,[&]{tap(&host,confirm,80);});
    QTimer::singleShot(2000,&outer,&QDialog::reject);
    check(outer.exec()==QDialog::Accepted && nestedAccepted,"nested dialogs must retain their results and accept finger input");
    check(!host.property("drnasEmbeddedModalActive").toBool(),"all dialog modality must clear after closing");
    std::cout<<"Portrait layout and repeated modal touch checks passed\n";
    host.takeCentralWidget();
}
#include "portrait-modal.moc"
