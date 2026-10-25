// SPDX-License-Identifier: LGPL-2.1-or-later
// A 4:3 PC desktop must fit the Android surface with exact touch hit-testing.
#include "../DrnasDesktopWorkspace.h"
#include "../DrnasTouchInput.h"
#include <QTest>
#include <QPushButton>
#include <QWindow>
#include <iostream>
namespace DrnasDiagnostics { void note(const QString& s) { qWarning().noquote()<<s; } }
static void require(bool good,const char* why) { if (!good) qFatal("%s",why); }
int main(int argc,char** argv) {
    qputenv("DRNAS_PC_DESKTOP_MODE","1");
    QApplication app(argc,argv);
    const QSize profile=DrnasDesktopWorkspace::startupSize();
    QMainWindow host,cad;
    host.resize(1680,768);
    auto* desktop=new DrnasDesktopWorkspace(&host);
    cad.setMinimumSize(0,0);
    host.setCentralWidget(desktop);
    desktop->attach(&cad);
    auto* content=new QWidget(&cad);
    cad.setCentralWidget(content);
    auto* button=new QPushButton("New",content);
    button->setGeometry(90,90,100,45);
    int clicks=0;
    QObject::connect(button,&QPushButton::clicked,[&]{++clicks;});
    new DrnasTouchInput(&host,&cad,[](const QString&){});
    host.show();cad.show();QTest::qWait(150);
    require(cad.size()==profile,"CAD content must have the selected logical profile size");
    const QPoint inset=cad.mapTo(&host,QPoint(0,0));
    require(inset.x()>200,"wide phone must center CAD with untouched side gutters");
    const QPoint inside=button->rect().center();
    const QPoint hostTouch=host.mapFromGlobal(button->mapToGlobal(inside));
    require(hostTouch.x()>inset.x(),"touch must include the centered desktop translation");
    auto* device=QTest::createTouchDevice();
    QTest::touchEvent(&host,device).press(91,hostTouch,&host).commit();
    QTest::touchEvent(&host,device).release(91,hostTouch,&host).commit();
    QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();
    require(clicks==1,"touch on QtActivity host must activate offset desktop button exactly once");
    host.resize(1200,420);QTest::qWait(140);
    require(cad.size().width()<=profile.width() && cad.size().height()<=420,
            "smaller surfaces must shrink the logical CAD uniformly without clipping");
    const double aspect=double(cad.width())/cad.height();
    require(std::abs(aspect-(1024.0/768))<0.01,"CAD content cannot stretch or distort");
    const QPoint after=host.mapFromGlobal(button->mapToGlobal(button->rect().center()));
    QTest::touchEvent(&host,device).press(92,after,&host).commit();
    QTest::touchEvent(&host,device).release(92,after,&host).commit();
    QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();
    require(clicks==2,"desktop touch must still hit the widget after scale-to-fit");
    host.takeCentralWidget(); // stack-owned cad is owned by the wrapper
    std::cout<<"Selected desktop size, 4:3 fit and centered touch checks passed\n";
}
