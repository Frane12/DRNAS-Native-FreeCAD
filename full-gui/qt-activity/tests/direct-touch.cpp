// SPDX-License-Identifier: LGPL-2.1-or-later
// Desktop Qt event routing regression checks; no FreeCAD/GL dependencies.
#include "../DrnasTouchInput.h"
#include <QTest>
#include <iostream>
namespace DrnasDiagnostics { void note(const QString& s) { qWarning().noquote()<<s; } }
struct Viewport : QWidget {
    int presses=0, releases=0, moves=0;
    QPointF pressPosition;
    Qt::KeyboardModifiers pressModifiers;
    bool eventFilter(QObject*,QEvent* e) override {
        if(e->type()==QEvent::MouseButtonPress || e->type()==QEvent::MouseButtonRelease || e->type()==QEvent::MouseMove) return event(e);
        return false;
    }
    bool event(QEvent* e) override {
        if(e->type()==QEvent::MouseButtonPress) {
            auto* m=static_cast<QMouseEvent*>(e); ++presses;
            pressPosition=m->position(); pressModifiers=m->modifiers(); e->accept();return true;
        }
        if(e->type()==QEvent::MouseButtonRelease) {++releases;e->accept();return true;}
        if(e->type()==QEvent::MouseMove) {++moves;e->accept();return true;}
        return QWidget::event(e);
    }
};
void check(bool ok,const char* message) { if(!ok) qFatal("%s",message); }
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QMainWindow host, cad;
    host.resize(1000,600); host.setCentralWidget(&cad);
    auto* graphics=new QGraphicsView(&cad);
    graphics->setProperty("drnasCadViewport",true);
    auto* viewport=new Viewport;
    graphics->setViewport(viewport); cad.setCentralWidget(graphics);
    QStringList cameraScripts;
    bool tool=false;
    auto* input=new DrnasTouchInput(&host,&cad,[&](const QString& code){
        if(code.contains("setProperty")) cad.setProperty("drnasTouchToolActive",tool);
        else cameraScripts.append(code);
    });
    Q_UNUSED(input);
    viewport->installEventFilter(viewport);
    host.show(); QTest::qWait(20);
    auto point=[&](int id,QEventPoint::State state,QPoint p){
        return QEventPoint(id,state,QPointF(p),QPointF(viewport->mapToGlobal(p)));
    };
    auto* device=QTest::createTouchDevice();
    auto touch=[&](QEvent::Type,QList<QEventPoint> points){
        auto sequence=QTest::touchEvent(viewport,device,false);
        for(const auto& p:points) {
            const QPoint local=viewport->mapFromGlobal(p.globalPosition().toPoint());
            switch(p.state()) {
            case QEventPoint::Pressed: sequence.press(p.id(),local,viewport);break;
            case QEventPoint::Released: sequence.release(p.id(),local,viewport);break;
            case QEventPoint::Updated: sequence.move(p.id(),local,viewport);break;
            default: sequence.stationary(p.id());break;
            }
        }
        sequence.commit();
    };
    auto drain=[] { QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents(); };
    touch(QEvent::TouchBegin,{point(1,QEventPoint::Pressed,{120,140})});
    check(viewport->presses==0,"touch-down must defer selection");
    touch(QEvent::TouchEnd,{point(1,QEventPoint::Released,{120,140})});drain();
    check(viewport->presses==1 && viewport->releases==1,"tap must produce one balanced click");
    check(viewport->pressPosition==QPointF(120,140),"tap must use absolute viewport coordinates");
    touch(QEvent::TouchBegin,{point(1,QEventPoint::Pressed,{120,140})});
    touch(QEvent::TouchUpdate,{point(1,QEventPoint::Updated,{180,170})});
    touch(QEvent::TouchEnd,{point(1,QEventPoint::Released,{200,180})});drain();
    check(viewport->presses==1,"orbit must not select geometry");
    check(!cameraScripts.isEmpty(),"orbit must update the actual camera");
    cameraScripts.clear();
    touch(QEvent::TouchBegin,{point(1,QEventPoint::Pressed,{120,140})});
    touch(QEvent::TouchUpdate,{point(1,QEventPoint::Stationary,{120,140}),point(2,QEventPoint::Pressed,{220,140})});
    touch(QEvent::TouchUpdate,{point(1,QEventPoint::Updated,{100,160}),point(2,QEventPoint::Updated,{260,160})});
    touch(QEvent::TouchUpdate,{point(1,QEventPoint::Released,{100,160}),point(2,QEventPoint::Stationary,{260,160})});
    touch(QEvent::TouchUpdate,{point(2,QEventPoint::Updated,{270,170})});
    touch(QEvent::TouchEnd,{point(2,QEventPoint::Released,{270,170})});drain();
    check(viewport->presses==1,"pinch and remaining finger must not click");
    check(!cameraScripts.isEmpty(),"two-finger pan/pinch must update camera");
    tool=true;
    const int scriptsBefore=cameraScripts.size();
    touch(QEvent::TouchBegin,{point(1,QEventPoint::Pressed,{120,140})});
    touch(QEvent::TouchUpdate,{point(1,QEventPoint::Updated,{180,170})});
    touch(QEvent::TouchEnd,{point(1,QEventPoint::Released,{200,180})});drain();
    check(viewport->presses==2 && viewport->releases==2,"tool drag needs balanced press/release");
    check(cameraScripts.size()==scriptsBefore,"tool drag must not orbit");
    touch(QEvent::TouchBegin,{point(1,QEventPoint::Pressed,{120,140})});
    touch(QEvent::TouchUpdate,{point(1,QEventPoint::Updated,{180,170})});
    QEvent cancel(QEvent::TouchCancel); QCoreApplication::sendEvent(viewport,&cancel);drain();
    check(viewport->presses==3 && viewport->releases==3,"TouchCancel must release a tool drag");
    tool=false;
    touch(QEvent::TouchBegin,{point(1,QEventPoint::Pressed,{120,140})});
    QTest::qWait(600);
    touch(QEvent::TouchEnd,{point(1,QEventPoint::Released,{120,140})});drain();
    check(viewport->presses==4 && viewport->releases==4,"precision selection needs one click");
    check(viewport->pressPosition==QPointF(120,98),"precision cursor must be above the finger");
    touch(QEvent::TouchBegin,{point(1,QEventPoint::Pressed,{120,140})});
    QDialog dialog(&host);dialog.setModal(true);dialog.show();drain();
    dialog.hide();drain();
    touch(QEvent::TouchEnd,{point(1,QEventPoint::Released,{120,140})});drain();
    check(viewport->presses==4,"opening a dialog must cancel pending selection");
    auto* button=new QPushButton("native",&cad);button->setGeometry(5,5,80,40);button->show();
    int clicks=0;QObject::connect(button,&QPushButton::clicked,[&]{++clicks;});
    QTest::mouseClick(button,Qt::LeftButton);check(clicks==1,"native buttons must stay directly operable");
    const int beforeSynthetic=viewport->presses;
    const QPointF global=viewport->mapToGlobal(QPoint(100,100));
    QMouseEvent duplicate(QEvent::MouseButtonPress,QPointF(100,100),QPointF(100,100),global,
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier,Qt::MouseEventSynthesizedByQt);
    QCoreApplication::sendEvent(viewport,&duplicate);
    check(viewport->presses==beforeSynthetic,"synthetic duplicate must not double-click");
    auto* multi=host.findChild<QPushButton*>("drnasTouchMultiSelect");
    multi->setChecked(true);
    touch(QEvent::TouchBegin,{point(1,QEventPoint::Pressed,{120,140})});
    touch(QEvent::TouchEnd,{point(1,QEventPoint::Released,{120,140})});drain();
    check(viewport->pressModifiers.testFlag(Qt::ControlModifier),"multi-selection must add Ctrl to selection");
    check(multi->isChecked(),"multi-selection must stay enabled across taps");
    auto* cancelButton=host.findChild<QPushButton*>("drnasTouchCancel");
    check(cancelButton && cancelButton->isVisible(),"cancel must remain visible with panel closed");
    std::cout<<"Direct touch routing checks passed\n";
    host.takeCentralWidget(); // stack-owned cad
}
