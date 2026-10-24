// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <algorithm>
#include <functional>
#include <utility>
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QComboBox>
#include <QLineEdit>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWheelEvent>
#include "StartupDiagnostics.h"

class DrnasPointerMark final : public QWidget {
public:
    explicit DrnasPointerMark(QWidget* parent):QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents); setAttribute(Qt::WA_NoSystemBackground);
        setFixedSize(25,25);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        for(const auto& pen:{QPen(Qt::black,4),QPen(QColor(110,220,255),2)}) {
            p.setPen(pen); p.drawEllipse(QPointF(12,12),5,5);
            p.drawLine(12,0,12,8); p.drawLine(12,16,12,24);
            p.drawLine(0,12,8,12); p.drawLine(16,12,24,12);
        }
    }
};

// Each control has its own drag strip; moving it never sends a CAD click.
class DrnasMovableInput final : public QWidget {
    QWidget* host;
    QLabel* grip;
    QString key;
    QPoint offset;
    bool dragging=false;
    QPointF initial;
    QString setting(const char* axis) const {
        return QStringLiteral("touch/minimal/%1/%2/%3").arg(key,
            host->height()>host->width()?QStringLiteral("portrait"):QStringLiteral("landscape"),QString::fromLatin1(axis));
    }
    void save() {
        QSettings s;
        s.setValue(setting("x"),double(x())/std::max(1,host->width()-width()));
        s.setValue(setting("y"),double(y())/std::max(1,host->height()-height()));
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if(object!=grip) return false;
        if(event->type()==QEvent::MouseButtonPress) {
            auto* e=static_cast<QMouseEvent*>(event);
            if(e->button()==Qt::LeftButton) {dragging=true;offset=e->globalPosition().toPoint()-mapToGlobal(QPoint());return true;}
        }
        if(event->type()==QEvent::MouseMove && dragging) {
            auto* e=static_cast<QMouseEvent*>(event);
            move(host->mapFromGlobal(e->globalPosition().toPoint()-offset)); clamp(); return true;
        }
        if(event->type()==QEvent::MouseButtonRelease && dragging) {dragging=false;save();return true;}
        return false;
    }
public:
    QVBoxLayout* body;
    DrnasMovableInput(QWidget* parent,const QString& id,QSize size,QPointF position)
        :QWidget(parent),host(parent),key(id),initial(position) {
        setObjectName(QStringLiteral("drnasInput_")+id); setFixedSize(size);
        setStyleSheet(QStringLiteral("QWidget{background:#203a50;color:white;border-radius:7px;}"
            "QPushButton{background:#315775;color:white;border:1px solid #6d91ab;border-radius:6px;font-weight:bold;}"
            "QPushButton:pressed{background:#218ca4;}QLabel{color:#a6c3d6;font-size:10px;}"));
        body=new QVBoxLayout(this);body->setContentsMargins(3,0,3,3);body->setSpacing(0);
        grip=new QLabel(QStringLiteral("···"),this);grip->setAlignment(Qt::AlignCenter);grip->setFixedHeight(13);
        grip->setToolTip(QStringLiteral("Povuci za pomicanje kontrole"));grip->installEventFilter(this);body->addWidget(grip);
        restore(); show();
    }
    void clamp() {
        move(std::clamp(x(),0,std::max(0,host->width()-width())),std::clamp(y(),0,std::max(0,host->height()-height())));raise();
    }
    void restore() {
        QSettings s;move(qRound(s.value(setting("x"),initial.x()).toDouble()*std::max(0,host->width()-width())),
                         qRound(s.value(setting("y"),initial.y()).toDouble()*std::max(0,host->height()-height())));clamp();
    }
};

class DrnasMouseKey final : public QPushButton {
    std::function<void(bool)> change;
    bool held=false;
    void updateHeld(bool down) {if(held==down)return;held=down;setDown(down);change(down);}
public:
    DrnasMouseKey(const QString& text,QWidget* parent,std::function<void(bool)> callback)
        :QPushButton(text,parent),change(std::move(callback)) {
        setAttribute(Qt::WA_AcceptTouchEvents);setFocusPolicy(Qt::NoFocus);
        setMinimumSize(0,0);setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);
    }
    void release() {updateHeld(false);}
protected:
    bool event(QEvent* event) override {
        if(event->type()==QEvent::TouchCancel){updateHeld(false);event->accept();return true;}
        if(event->type()==QEvent::TouchBegin || event->type()==QEvent::TouchUpdate || event->type()==QEvent::TouchEnd) {
            for(const auto& point:static_cast<QTouchEvent*>(event)->points()) {
                if(point.state()==QEventPoint::State::Pressed)updateHeld(true);
                if(point.state()==QEventPoint::State::Released)updateHeld(false);
            }
            event->accept();return true;
        }
        return QPushButton::event(event);
    }
    void mousePressEvent(QMouseEvent* event) override {updateHeld(true);event->accept();}
    void mouseReleaseEvent(QMouseEvent* event) override {updateHeld(false);event->accept();}
};

class DrnasTouchInput final : public QObject {
    QMainWindow* host;
    QPointer<QMainWindow> cad;
    QList<DrnasMovableInput*> controls;
    QList<DrnasMouseKey*> mouseKeys;
    QPointer<QSlider> zoomControl;
    DrnasPointerMark* mark;
    QPointer<DrnasPointerMark> dialogMark;
    QPointer<QWidget> mouseReceiver;
    QPointF cursor,last,start;
    QElapsedTimer clock;
    bool relative=false,moved=false,repairQueued=false;
    int touchId=-1,queuedZoom=0,previousZoom=0;
    Qt::MouseButtons buttons=Qt::NoButton;
    QTimer zoomTimer;
    QPoint globalPoint() const {return host->mapToGlobal(cursor.toPoint());}
    QWidget* target() const {
        QWidget* top=QApplication::activePopupWidget();
        if(!top)top=QApplication::activeModalWidget();
        if(top) {auto* child=top->childAt(top->mapFromGlobal(globalPoint()));return child?child:top;}
        return QApplication::widgetAt(globalPoint());
    }
    void sendMouse(QEvent::Type type,Qt::MouseButton button) {
        QWidget* hit=mouseReceiver?mouseReceiver.data():target();if(!hit)return;
        const QPoint global=globalPoint();
        QCoreApplication::postEvent(hit,new QMouseEvent(type,hit->mapFromGlobal(global),
            hit->window()->mapFromGlobal(global),global,button,buttons,Qt::NoModifier));
    }
    void mouseButton(Qt::MouseButton button,bool down) {
        if(down) {
            if(buttons.testFlag(button))return;
            if(buttons==Qt::NoButton)mouseReceiver=target();
            buttons|=button;sendMouse(QEvent::MouseButtonPress,button);
        } else {
            if(!buttons.testFlag(button))return;
            buttons&=~Qt::MouseButtons(button);sendMouse(QEvent::MouseButtonRelease,button);
            if(buttons==Qt::NoButton)mouseReceiver.clear();
        }
    }
    void releaseAll() {
        mouseButton(Qt::LeftButton,false);mouseButton(Qt::RightButton,false);
        for(auto* key:mouseKeys)key->release();
        relative=false;touchId=-1;queuedZoom=0;previousZoom=0;zoomTimer.stop();mouseReceiver.clear();
        if(zoomControl){QSignalBlocker blocker(zoomControl);zoomControl->setValue(0);}
    }
    bool isControl(QWidget* widget) const {
        for(auto* control:controls)if(widget==control || control->isAncestorOf(widget))return true;
        return false;
    }
    bool inRender(QWidget* widget) const {
        if(!cad || !widget || QApplication::activeModalWidget() || QApplication::activePopupWidget())return false;
        bool rendering=false;
        for(QWidget* p=widget;p && p!=host;p=p->parentWidget()) {
            if(qobject_cast<QDialog*>(p)||qobject_cast<QDockWidget*>(p)||qobject_cast<QAbstractButton*>(p)||
               qobject_cast<QAbstractItemView*>(p)||qobject_cast<QAbstractSlider*>(p)||qobject_cast<QComboBox*>(p)||qobject_cast<QLineEdit*>(p))return false;
            if(qobject_cast<QGraphicsView*>(p)||qobject_cast<QOpenGLWidget*>(p))rendering=true;
        }
        return rendering && cad->isAncestorOf(widget);
    }
    void showPointer() {
        cursor.setX(std::clamp(cursor.x(),0.0,double(std::max(0,host->width()-1))));
        cursor.setY(std::clamp(cursor.y(),0.0,double(std::max(0,host->height()-1))));
        QWidget* top=QApplication::activePopupWidget();if(!top)top=QApplication::activeModalWidget();
        if(top && top->rect().contains(top->mapFromGlobal(globalPoint()))) {
            mark->hide();
            if(!dialogMark || dialogMark->parentWidget()!=top) {
                if(dialogMark)dialogMark->hide();dialogMark=new DrnasPointerMark(top);
            }
            dialogMark->move(top->mapFromGlobal(globalPoint())-QPoint(12,12));dialogMark->show();dialogMark->raise();
        } else {
            if(dialogMark)dialogMark->hide();mark->move(cursor.toPoint()-QPoint(12,12));mark->show();mark->raise();
        }
    }
    void begin(const QPointF& point){relative=true;moved=false;start=last=point;clock.start();}
    void move(const QPointF& point) {
        if(!relative)return;
        if((point-start).manhattanLength()>QApplication::startDragDistance())moved=true;
        cursor+=point-last;last=point;showPointer();sendMouse(QEvent::MouseMove,Qt::NoButton);
    }
    void end(const QPointF& point) {
        if(!relative)return;move(point);relative=false;
        if(!moved && clock.elapsed()<350 && buttons==Qt::NoButton) {
            mouseButton(Qt::LeftButton,true);mouseButton(Qt::LeftButton,false);
        }
    }
    void zoomStep() {
        if(!cad || QApplication::activeModalWidget()){queuedZoom=0;zoomTimer.stop();return;}
        QWidget* view=nullptr;
        if(auto* mdi=cad->findChild<QMdiArea*>())if(auto* active=mdi->activeSubWindow()) {
            for(auto* candidate:active->findChildren<QGraphicsView*>())if(candidate->isVisible()){view=candidate->viewport();break;}
        }
        if(!view){queuedZoom=0;zoomTimer.stop();return;}
        const int delta=std::clamp(queuedZoom,-60,60);queuedZoom-=delta;
        const QPoint local=view->rect().center();
        QCoreApplication::postEvent(view,new QWheelEvent(local,view->mapToGlobal(local),QPoint(),QPoint(0,delta),
            Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false));
        if(!queuedZoom)zoomTimer.stop();
    }
    void repair() {
        repairQueued=false;
        if(!cad)return;
        for(auto* area:cad->findChildren<QMdiArea*>()) {
            area->setOption(QMdiArea::DontMaximizeSubWindowOnActivation,false);
            if(auto* active=area->activeSubWindow())if(active->isVisible()&&!active->isMaximized())active->showMaximized();
        }
        // Overlay proxy shells can survive dock restoration as white frames.
        // Docks themselves are ordinary, usable panels outside the GL viewport.
        for(const char* name:{"OverlayLeft","OverlayRight","OverlayTop","OverlayBottom"}) {
            for(const QString& suffix:{QString(),QStringLiteral("Proxy")})
                if(auto* shell=cad->findChild<QWidget*>(QString::fromLatin1(name)+suffix))
                    if(shell->findChildren<QDockWidget*>().isEmpty())shell->hide();
        }
        const QRect bounds(host->mapToGlobal(QPoint()),host->size());
        for(auto* top:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QDialog*>(top)) {
            if(!dialog->isVisible() || !dialog->parentWidget() ||
               !(dialog->parentWidget()==host || host->isAncestorOf(dialog->parentWidget())))continue;
            dialog->setMinimumSize(0,0);dialog->setMaximumSize(bounds.size());
            dialog->resize(dialog->size().boundedTo(bounds.size()));
            dialog->move(bounds.center()-QPoint(dialog->width()/2,dialog->height()/2));
        }
        for(auto* control:controls)control->clamp();
    }
    void scheduleRepair(){if(repairQueued)return;repairQueued=true;QTimer::singleShot(0,this,[this]{repair();});}
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        auto* widget=qobject_cast<QWidget*>(object);
        const auto type=event->type();
        if(type==QEvent::ApplicationDeactivate || (object==host && type==QEvent::Hide))releaseAll();
        if(object==host && type==QEvent::Resize) {
            releaseAll();for(auto* control:controls)control->restore();
            cursor=QPointF(host->width()/2.0,host->height()/2.0);scheduleRepair();
        }
        if((type==QEvent::Show || type==QEvent::Resize) &&
            (qobject_cast<QDialog*>(object)||qobject_cast<QMdiSubWindow*>(object)||
             (widget && widget->objectName().startsWith(QStringLiteral("Overlay")))))scheduleRepair();
        if(!widget || isControl(widget))return false;
        // Ordinary Qt controls, especially dialog buttons, keep direct touch.
        // Only the 3D surface behaves as a relative touchpad.
        if(!inRender(widget))return false;
        if(type==QEvent::TouchCancel){releaseAll();event->accept();return true;}
        if(type==QEvent::TouchBegin || type==QEvent::TouchUpdate || type==QEvent::TouchEnd) {
            for(const auto& point:static_cast<QTouchEvent*>(event)->points()) {
                if(touchId<0 && point.state()==QEventPoint::State::Pressed){touchId=point.id();begin(point.globalPosition());}
                if(point.id()!=touchId)continue;
                if(point.state()==QEventPoint::State::Released){end(point.globalPosition());touchId=-1;}else move(point.globalPosition());
            }
            event->accept();return true;
        }
        if(type==QEvent::MouseButtonPress || type==QEvent::MouseMove || type==QEvent::MouseButtonRelease) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(!event->spontaneous() && mouse->source()==Qt::MouseEventNotSynthesized)return false;
            if(mouse->source()!=Qt::MouseEventNotSynthesized) {
                if(touchId<0) {
                    if(type==QEvent::MouseButtonPress)begin(mouse->globalPosition());
                    else if(type==QEvent::MouseButtonRelease)end(mouse->globalPosition());else move(mouse->globalPosition());
                }
                event->accept();return true;
            }
        }
        return false;
    }
public:
    DrnasTouchInput(QMainWindow* shell,QMainWindow* freecad,std::function<void(const QString&)>)
        :QObject(shell),host(shell),cad(freecad) {
        mark=new DrnasPointerMark(host);cursor=QPointF(host->width()/2.0,host->height()/2.0);
        for(auto entry:{std::pair<const char*,Qt::MouseButton>{"L",Qt::LeftButton},{"R",Qt::RightButton}}) {
            auto* group=new DrnasMovableInput(host,QString::fromLatin1(entry.first),QSize(54,57),QPointF(entry.second==Qt::LeftButton?0.02:(host->height()>host->width()?0.23:0.10),0.9));
            auto* button=new DrnasMouseKey(QString::fromLatin1(entry.first),group,[this,entry](bool down){mouseButton(entry.second,down);});
            group->body->addWidget(button,1);controls.append(group);mouseKeys.append(button);
        }
        auto* zoomGroup=new DrnasMovableInput(host,QStringLiteral("zoom"),QSize(158,51),QPointF(0.96,0.94));controls.append(zoomGroup);
        auto* zoom=new QSlider(Qt::Horizontal,zoomGroup);zoomControl=zoom;zoom->setRange(-100,100);zoom->setValue(0);zoom->setFocusPolicy(Qt::NoFocus);
        zoom->setToolTip(QStringLiteral("Zoom — vraća se u sredinu"));zoomGroup->body->addWidget(zoom,1);
        zoom->setStyleSheet(QStringLiteral("QSlider::groove:horizontal{height:5px;background:#6d91ab;}QSlider::handle:horizontal{background:#73d9ec;width:20px;margin:-9px 0;border-radius:6px;}"));
        connect(zoom,&QSlider::sliderMoved,this,[this](int value){queuedZoom=std::clamp(queuedZoom+(value-previousZoom)*12,-1200,1200);previousZoom=value;if(queuedZoom)zoomTimer.start();});
        connect(zoom,&QSlider::sliderReleased,this,[this,zoom]{QSignalBlocker block(zoom);zoom->setValue(0);previousZoom=0;});
        zoomTimer.setInterval(16);connect(&zoomTimer,&QTimer::timeout,this,[this]{zoomStep();});
        qApp->installEventFilter(this);scheduleRepair();showPointer();
        DrnasDiagnostics::note(QStringLiteral("INPUT minimal: movable L/R + zoom; direct dialogs; relative CAD viewport"));
    }
};
