// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../DrnasAndroidCommandUi.h"
#include "../DrnasTouchInput.h"
#include <QTest>
#include <QWindow>
#include <QAbstractItemView>
#include <iostream>
namespace DrnasDiagnostics { void note(const QString& s) {qWarning().noquote()<<s;} }
static void require(bool ok,const char* why) {if (!ok) qFatal("%s",why);}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QMainWindow host,cad;
    host.resize(1000,600);host.setCentralWidget(&cad);
    auto* viewport=new QWidget(&cad);viewport->setObjectName("originalCoinViewport");
    cad.setCentralWidget(viewport);
    auto* file=cad.menuBar()->addMenu("File");
    auto* bar=new QToolBar("File",&cad);cad.addToolBar(bar);
    auto* action=new QAction("&New file",&cad);action->setObjectName("Std_New");
    action->setShortcut(QKeySequence("Ctrl+N"));
    QPixmap icon(24,24);icon.fill(Qt::green);action->setIcon(QIcon(icon));
    action->setToolTip("Original FreeCAD New");file->addAction(action);bar->addAction(action);
    int created=0;QObject::connect(action,&QAction::triggered,[&]{++created;});
    auto* toggle=bar->addAction("Selection mode");toggle->setObjectName("Test_Toggle");toggle->setCheckable(true);
    auto* group=bar->addAction("Primitive");group->setObjectName("Part_Primitives");
    auto* sourceButton=qobject_cast<QToolButton*>(bar->widgetForAction(group));
    auto* originalPopup=new QMenu(sourceButton);originalPopup->addAction("Cylinder");
    sourceButton->setMenu(originalPopup);sourceButton->setPopupMode(QToolButton::MenuButtonPopup);
    // Populate overflow so a real finger drag over buttons must scroll.
    for (int i=0;i<18;++i) bar->addAction(QStringLiteral("Tool %1").arg(i));
    auto* later=new QToolBar("Sketcher",&cad);later->addAction("Line");cad.addToolBar(later);later->hide();
    new DrnasTouchInput(&host,&cad,[](const QString&){qFatal("command UI cannot replace FreeCAD commands with Python");});
    host.takeCentralWidget();
    auto* ui=new DrnasAndroidCommandUi(&host,&cad);host.setCentralWidget(ui);
    host.show();cad.show();QTest::qWait(160);
    auto drain=[] {QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();};
    auto* device=QTest::createTouchDevice();QWindow* window=host.windowHandle();
    require(window,"one native host surface required");
    auto tap=[&](QWidget* target,int id) {
        const QPoint p=window->mapFromGlobal(target->mapToGlobal(target->rect().center()));
        QTest::touchEvent(window,device).press(id,p,window).commit();
        QTest::touchEvent(window,device).release(id,p,window).commit();drain();
    };
    auto all=ui->findChildren<QToolButton*>("drnasAndroidAction_Std_New");
    require(!all.isEmpty(),"native New command must be represented in the Android UI");
    auto* copy=all.first();
    require(copy->defaultAction()==action && action->parent()==&cad,"FreeCAD must retain ownership of the SAME action");
    require(copy->icon().cacheKey()==action->icon().cacheKey(),"original icon must be reused unchanged");
    require(copy->toolTip()==action->toolTip(),"original command tooltip must survive");
    require(copy->width()>=48 && copy->height()>=48,"Android targets must be at least 48 logical pixels");
    require(cad.centralWidget()==viewport && viewport->parentWidget()==&cad,"original render widget must not be replaced/reparented");
    require(cad.width()==ui->width(),"Android CAD must use full available width");
    require(bar->size()==QSize(0,0) && !bar->isHidden(),"source chrome must collapse without changing FreeCAD visibility state");
    require(host.size()==QSize(1000,600),"original toolbar minimums cannot enlarge the native host");
    require(!cad.isWindow() && cad.windowHandle()==nullptr,"CAD cannot create a second native window");
    const QString preview=qEnvironmentVariable("DRNAS_UI_PREVIEW");
    if (!preview.isEmpty()) require(host.grab().save(preview),"preview image must save");
    tap(copy,1);require(created==1,"native Android New tap must invoke original QAction once");
    tap(copy,2);require(created==1,"shared command buttons must retain finger debounce");
    QTest::keyClick(&cad,Qt::Key_N,Qt::ControlModifier);drain();
    require(created==2,"a shared action must retain one original keyboard shortcut without ambiguity");
    action->setEnabled(false);drain();require(!copy->isEnabled(),"original enabled state must propagate");
    action->setEnabled(true);action->setText("Translated New");drain();
    require(copy->isEnabled() && copy->text()=="Translated New","translation and enabled state must remain live");
    icon.fill(Qt::blue);action->setIcon(QIcon(icon));drain();
    require(copy->icon().cacheKey()==action->icon().cacheKey(),"a later original icon change must synchronize");
    auto* toggled=ui->findChild<QToolButton*>("drnasAndroidAction_Test_Toggle");
    require(toggled && toggled->defaultAction()==toggle,"checkable command must retain action identity");
    tap(toggled,3);require(toggle->isChecked() && toggled->isChecked(),"native QAction check state must synchronize");
    auto* dropdown=ui->findChild<QToolButton*>("drnasAndroidAction_Part_Primitives");
    require(dropdown && !group->menu() && dropdown->menu()==originalPopup,
        "ActionGroup popup attached to source button must survive even without QAction::menu");
    require(dropdown->popupMode()==sourceButton->popupMode(),"original dropdown mode must survive");
    auto* groups=ui->findChild<QComboBox*>("drnasAndroidGroups");
    require(groups && groups->findText("Sketcher")<0,"hidden workbench toolbar must not appear in the palette");
    auto* area=qobject_cast<QScrollArea*>(toggled->parentWidget()->parentWidget()->parentWidget());
    require(area && area->horizontalScrollBar()->maximum()>100,"tools must overflow into a horizontal scroller");
    const QPoint start=window->mapFromGlobal(toggled->mapToGlobal(toggled->rect().center()));
    QTest::touchEvent(window,device).press(4,start,window).commit();
    QTest::touchEvent(window,device).move(4,start-QPoint(80,0),window).commit();
    QTest::touchEvent(window,device).release(4,start-QPoint(80,0),window).commit();drain();
    require(area->horizontalScrollBar()->value()>0,"finger drag beginning on a tool must scroll the Android strip");
    require(toggle->isChecked(),"scrolling a command strip cannot trigger the touched tool");
    later->show();bar->hide();QTest::qWait(150);
    require(groups->findText("Sketcher")>=0 && groups->findText("File")==groups->count()-1,
        "workbench show/hide must update toolbar groups while preserving the File menu group");
    require(later->size()==QSize(0,0),"late workbench toolbar must also collapse");
    const int sketch=groups->findText("Sketcher");
    groups->setCurrentIndex(groups->findText("File"));drain();
    tap(groups,5);drain();
    auto* list=groups->view();require(list->isVisible(),"finger must open the Android group selector");
    auto* popup=list->window()->windowHandle();require(popup,"selector popup surface required");
    const QPoint item=popup->mapFromGlobal(list->viewport()->mapToGlobal(list->visualRect(list->model()->index(sketch,0)).center()));
    QTest::touchEvent(popup,device).press(6,item,popup).commit();
    QTest::touchEvent(popup,device).release(6,item,popup).commit();drain();
    require(groups->currentIndex()==sketch,"finger must select a different tool group");
    require(ui->findChildren<QToolButton*>().size()>0,"new workbench tools must have Android controls");
    // No local command implementation or duplicate action may be introduced.
    require(cad.findChildren<QAction*>("Std_New").size()==1,"there must be one authoritative Std_New action");
    host.takeCentralWidget();cad.setParent(nullptr);delete ui;
    std::cout<<"Android command identity, icons, state, native touch, scroll and workbench checks passed\n";
}
