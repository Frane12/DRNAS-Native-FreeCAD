// SPDX-License-Identifier: LGPL-2.1-or-later
// Exercise the native-window entry used by Android, rather than QAction::trigger().
#include "../DrnasDesktopWorkspace.h"
#include "../DrnasTouchInput.h"
#include <QTest>
#include <QWindow>
#include <iostream>
namespace DrnasDiagnostics { void note(const QString& s) { qWarning().noquote()<<s; } }
static void require(bool ok,const char* why) { if (!ok) qFatal("%s",why); }
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QMainWindow host,cad;
    host.resize(1400,768);
    auto* workspace=new DrnasDesktopWorkspace(&host);
    host.setCentralWidget(workspace);workspace->attach(&cad);
    cad.setCentralWidget(new QWidget(&cad));
    auto* bar=new QToolBar("Original FreeCAD toolbar",&cad);cad.addToolBar(bar);
    auto* create=bar->addAction("New file");int created=0;
    auto* other=bar->addAction("Other command");int others=0;
    QObject::connect(create,&QAction::triggered,[&]{++created;});
    QObject::connect(other,&QAction::triggered,[&]{++others;});
    new DrnasTouchInput(&host,&cad,[](const QString&){qFatal("UI must not invoke camera Python");});
    host.show();cad.show();QTest::qWait(100);
    auto* button=qobject_cast<QToolButton*>(bar->widgetForAction(create));
    auto* alternate=qobject_cast<QToolButton*>(bar->widgetForAction(other));
    auto* device=QTest::createTouchDevice();
    QWindow* window=host.windowHandle();require(window,"native input surface required");
    auto drain=[] {QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();};
    auto position=[&](QWidget* target){return window->mapFromGlobal(target->mapToGlobal(target->rect().center()));};
    auto tap=[&](QWidget* target,int id){
        const QPoint p=position(target);
        QTest::touchEvent(window,device).press(id,p,window).commit();
        QTest::touchEvent(window,device).move(id,p+QPoint(2,1),window).commit();
        QTest::touchEvent(window,device).release(id,p,window).commit();drain();
    };
    tap(button,1);require(created==1,"native toolbar tap must trigger New once despite finger jitter");
    tap(button,2);require(created==1,"same-control bounce inside 120 ms must not create a second file");
    tap(alternate,3);require(others==1,"debounce must not block a different command");
    QTest::qWait(140);tap(button,4);require(created==2,"New must work again after debounce expires");
    const QPointF global=button->mapToGlobal(button->rect().center());
    for (auto source:{Qt::MouseEventSynthesizedByQt,Qt::MouseEventSynthesizedBySystem,Qt::MouseEventNotSynthesized}) {
        for (auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent duplicate(type,button->rect().center(),host.mapFromGlobal(global),global,
                Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,
                Qt::NoModifier,source,device);
            QCoreApplication::sendEvent(button,&duplicate);
        }
    }
    require(created==2,"raw touch plus synthesized mouse must still activate only once");
    // Desktop USB/Bluetooth mouse input remains immediate and supports repeat clicks.
    QTest::mouseClick(button,Qt::LeftButton);QTest::mouseClick(button,Qt::LeftButton);
    require(created==4,"real mouse clicks must not use the finger debounce");
    QTest::qWait(140);
    QPoint p=position(button);
    QTest::touchEvent(window,device).press(5,p,window).commit();
    QEvent cancel(QEvent::TouchCancel);QCoreApplication::sendEvent(window,&cancel);drain();
    QTest::touchEvent(window,device).release(5,p,window).commit();drain();
    require(created==4,"TouchCancel must never trigger a toolbar command");
    tap(button,6);require(created==5,"fresh toolbar touch must recover after TouchCancel");
    QTest::qWait(140);
    p=position(button);
    QTest::touchEvent(window,device).press(7,p,window).commit();
    QEvent deactivate(QEvent::ApplicationDeactivate);QCoreApplication::sendEvent(qApp,&deactivate);
    QTest::touchEvent(window,device).release(7,p,window).commit();drain();
    require(created==5,"deactivation must discard a pending toolbar tap");
    tap(button,8);require(created==6,"first toolbar tap after resume must work");
    QTest::qWait(140);
    p=position(button);
    QTest::touchEvent(window,device).press(9,p,window).commit();
    QTest::touchEvent(window,device).move(9,p+QPoint(100,80),window).commit();
    QTest::touchEvent(window,device).release(9,p+QPoint(100,80),window).commit();drain();
    require(created==6,"sliding off the toolbar cannot activate New");
    host.takeCentralWidget();cad.setParent(nullptr);delete workspace;
    std::cout<<"Native toolbar routing, jitter, debounce, duplicate mouse and lifecycle checks passed\n";
}
