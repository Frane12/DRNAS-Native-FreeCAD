// SPDX-License-Identifier: LGPL-2.1-or-later
// Direct viewport touch, native Qt controls, and coalesced CAD camera gestures.
// Uses existing widgets and logical coordinates; creates no native/GL surface.
#pragma once
#include <algorithm>
#include <functional>
#include <utility>
#include <cmath>
#include <QLineF>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QTouchEvent>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include "StartupDiagnostics.h"

class DrnasPointerMark final : public QWidget {
public:
    explicit DrnasPointerMark(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFixedSize(25,25);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::black,4)); p.drawEllipse(QPointF(12,12),5,5);
        p.drawLine(12,0,12,8); p.drawLine(12,16,12,24);
        p.drawLine(0,12,8,12); p.drawLine(16,12,24,12);
        p.setPen(QPen(QColor(110,220,255),2)); p.drawEllipse(QPointF(12,12),5,5);
        p.drawLine(12,0,12,8); p.drawLine(12,16,12,24);
        p.drawLine(0,12,8,12); p.drawLine(16,12,24,12);
    }
};

class DrnasTouchInput final : public QObject {
    QMainWindow* host;
    QPointer<QMainWindow> cad;
    QWidget* panel;
    QPointer<QWidget> commandsPanel;
    QPointer<QWidget> commandsLauncher;
    QLabel* grip;
    QPushButton* launcher;
    QPushButton* toolMode;
    QPushButton* multiSelect;
    QPushButton* cancelButton;
    QPushButton* contextButton;
    DrnasPointerMark* mark;

    QSlider* zoom;
    QTimer zoomTimer;
    QPointer<QWidget> mouseReceiver;
    QPointer<QWidget> keyReceiver;
    QPointF cursor;
    QPoint dragOffset;
    enum class Gesture { Idle, Pending, Orbit, Tool, Precision, Multi };
    Gesture gesture=Gesture::Idle;
    QPointer<QWidget> touchOwner;
    QPointF touchStart, touchLast, multiCenter;
    int fingerId=-1;
    double multiDistance=0;
    bool toolAtStart=false;
    QTimer precisionTimer, cameraTimer;
    QPointF orbitDelta, panDelta;
    double zoomLog=0;
    QPointer<QWidget> cameraOwner;
    QPointer<QWidget> lastViewport;
    QPointer<QToolButton> toolbarTouchButton;
    QPointF toolbarTouchStart;
    int toolbarTouchId = -1;
    bool toolbarTouchCancelled = false;
    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    QList<QPushButton*> mouseKeys, modifierKeys;
    QSet<const QEvent*> injectedEvents;
    bool dragging=false, repairing=false, repairQueued=false;
    int queuedZoom=0, previousZoom=0;
    std::function<void(const QString&)> command;

    QPoint globalPoint() const { return host->mapToGlobal(cursor.toPoint()); }
    void deliverEvent(QWidget* target,QEvent* event) {
        // A click can enter QDialog::exec(). Ignore only this injected event,
        // so fresh finger input still works inside the nested dialog loop.
        injectedEvents.insert(event);
        QApplication::sendEvent(target,event);
        injectedEvents.remove(event);
    }
    QWidget* targetAtCursor() const {
        // Resolve the actual button/viewport at the last direct-touch position,
        // including modal dialogs, menus and the shell's own tool controls.
        QWidget* root=QApplication::activePopupWidget();
        if (!root) root=QApplication::activeModalWidget();
        if (root) {
            const QPoint p=root->mapFromGlobal(globalPoint());
            // QMenu must receive outside clicks too, so it can dismiss its
            // mouse grab. Other modal dialogs remain restricted to their rect.
            if (!root->rect().contains(p))
                return root==QApplication::activePopupWidget() ? root : nullptr;
            QWidget* child=root->childAt(p);
            return child ? child : root;
        }
        QWidget* hit=QApplication::widgetAt(globalPoint());
        if (hit && (hit==host || host->isAncestorOf(hit))) return hit;
        const QPoint p=host->mapFromGlobal(globalPoint());
        return host->rect().contains(p) ? host->childAt(p) : nullptr;
    }
    QWidget* keyboardTarget() const {
        QWidget* focus=QApplication::focusWidget();
        if (QWidget* popup=QApplication::activePopupWidget()) {
            if (focus && (focus==popup || popup->isAncestorOf(focus))) return focus;
            return popup;
        }
        if (QWidget* modal=QApplication::activeModalWidget()) {
            if (focus && (focus==modal || modal->isAncestorOf(focus))) return focus;
            return modal;
        }
        if (cad && focus && (focus==cad || cad->isAncestorOf(focus))) return focus;
        if (keyReceiver && cad && (keyReceiver.data()==cad.data() || cad->isAncestorOf(keyReceiver.data()))) return keyReceiver.data();
        return mouseReceiver ? mouseReceiver.data() : targetAtCursor();
    }
    void sendMouse(QEvent::Type type, Qt::MouseButton button) {
        QPointer<QWidget> target=mouseReceiver ? mouseReceiver.data() : targetAtCursor();
        if (!target) { buttons=Qt::NoButton; return; }
        const QPointF global=globalPoint();
        const QPointF local=target->mapFromGlobal(global.toPoint());
        const QPointF window=target->window()->mapFromGlobal(global.toPoint());
        // Posting lets both press and release be queued BEFORE a tool button
        // enters QMenu::exec(). A synchronous press could otherwise suspend
        // mouseButton(false) and leave an emulated button pressed.
        QCoreApplication::postEvent(target.data(),new QMouseEvent(
            type,local,window,global,button,buttons,modifiers));
    }
    void mouseButton(Qt::MouseButton button, bool down) {
        if (down) {
            if (buttons.testFlag(button)) return;
            if (buttons==Qt::NoButton) {
                if (!mouseReceiver) mouseReceiver=targetAtCursor();
                if (mouseReceiver) { mouseReceiver->setFocus(Qt::MouseFocusReason); keyReceiver=mouseReceiver; }
            }
            buttons|=button;
            sendMouse(QEvent::MouseButtonPress,button);
        } else {
            if (!buttons.testFlag(button)) return;
            buttons&=~Qt::MouseButtons(button);
            sendMouse(QEvent::MouseButtonRelease,button);
            if (buttons==Qt::NoButton) mouseReceiver.clear();
        }
    }
    void sendKey(int key, const QString& text=QString(), Qt::KeyboardModifiers extra=Qt::NoModifier) {
        QPointer<QWidget> target=keyboardTarget();
        if (!target) return;
        const auto flags=modifiers|extra;
        QKeyEvent press(QEvent::KeyPress,key,flags,text);
        QKeyEvent release(QEvent::KeyRelease,key,flags,text);
        deliverEvent(target,&press);
        if (target) deliverEvent(target,&release);
    }
    void modifier(Qt::KeyboardModifier flag, int key, bool down) {
        QPointer<QWidget> target=keyboardTarget();
        if (down) { if (!keyReceiver) keyReceiver=target; modifiers|=flag; }
        else modifiers&=~Qt::KeyboardModifiers(flag);
        if (target) {
            QKeyEvent event(down?QEvent::KeyPress:QEvent::KeyRelease,key,modifiers);
            deliverEvent(target,&event);
        }
    }
    void releaseAll() {
        for (auto button : {Qt::LeftButton,Qt::MiddleButton,Qt::RightButton}) mouseButton(button,false);
        if (modifiers.testFlag(Qt::ControlModifier)) modifier(Qt::ControlModifier,Qt::Key_Control,false);
        if (modifiers.testFlag(Qt::ShiftModifier)) modifier(Qt::ShiftModifier,Qt::Key_Shift,false);
        if (modifiers.testFlag(Qt::AltModifier)) modifier(Qt::AltModifier,Qt::Key_Alt,false);
        modifiers=Qt::NoModifier; buttons=Qt::NoButton;
        for (auto* b : mouseKeys+modifierKeys) { QSignalBlocker blocker(b); b->setChecked(false); }
        queuedZoom=0; previousZoom=0; zoomTimer.stop();
        if (zoom) { QSignalBlocker blocker(zoom); zoom->setValue(0); }
        mouseReceiver.clear();
    }
    void setPosition(const QPointF& global) {
        cursor=QPointF(host->mapFromGlobal(global.toPoint()));
    }
    // The original FreeCAD desktop toolbars consist of QToolButtons which
    // Qt/Android may not reliably turn into mouse clicks when the embedded
    // FreeCAD MainWindow shares a native surface with QtActivity. Opt these
    // actual upstream controls in, without making the whole host touch-accepting.
    void prepareFreeCADToolbars() {
        if (!cad) return;
        for (QToolBar* bar : cad->findChildren<QToolBar*>()) {
            bar->setAttribute(Qt::WA_AcceptTouchEvents, true);
            for (QToolButton* button : bar->findChildren<QToolButton*>())
                button->setAttribute(Qt::WA_AcceptTouchEvents, true);
        }
    }
    QToolButton* toolbarAtFinger(const QPointF& global) const {
        if (!cad || QApplication::activeModalWidget() || QApplication::activePopupWidget())
            return nullptr;
        // A real touch panel, native popup or lower shell control wins over
        // CAD toolbar fallback routing. No coordinate scaling (logical pixels).
        for (QWidget* overlay : std::initializer_list<QWidget*>{
                panel, commandsPanel.data(), commandsLauncher.data(),
                launcher, cancelButton, contextButton}) {
            if (overlay && overlay->isVisible() &&
                overlay->rect().contains(overlay->mapFromGlobal(global.toPoint())))
                return nullptr;
        }
        for (QToolBar* bar : cad->findChildren<QToolBar*>()) {
            if (!bar->isVisible() || !bar->isEnabled()) continue;
            if (!bar->rect().contains(bar->mapFromGlobal(global.toPoint()))) continue;
            for (QToolButton* button : bar->findChildren<QToolButton*>()) {
                if (button->isVisible() && button->isEnabled() &&
                    button->rect().contains(button->mapFromGlobal(global.toPoint())))
                    return button;
            }
        }
        return nullptr;
    }
    void clickFreeCADToolButton(QToolButton* button,const QPointF& global) {
        if (!button || !button->isVisible() || !button->isEnabled()) return;
        const QPointF local=button->mapFromGlobal(global.toPoint());
        const QPointF window=button->window()->mapFromGlobal(global.toPoint());
        // Both events are queued before executing QAction/QMenu handlers.
        // Avoid QMenu::exec() reentrancy inside QTouchEvent dispatch.
        QCoreApplication::postEvent(button,new QMouseEvent(QEvent::MouseButtonPress,
            local,window,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier));
        QCoreApplication::postEvent(button,new QMouseEvent(QEvent::MouseButtonRelease,
            local,window,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier));
        DrnasDiagnostics::note(QStringLiteral("INPUT TOOLBAR touch click action=%1 logical=%2,%3 dpr=%4")
            .arg(button->defaultAction() ? button->defaultAction()->text() : button->objectName())
            .arg(int(global.x())).arg(int(global.y())).arg(button->devicePixelRatioF()));
    }
    bool handleFreeCADToolbarTouch(QTouchEvent* event) {
        if (event->type()==QEvent::TouchBegin && toolbarTouchId<0) {
            if (event->points().size()!=1) return false;
            const QEventPoint& p=event->points().first();
            QToolButton* button=toolbarAtFinger(p.globalPosition());
            if (!button) return false;
            toolbarTouchButton=button;
            toolbarTouchId=p.id();
            toolbarTouchStart=p.globalPosition();
            toolbarTouchCancelled=false;
            DrnasDiagnostics::note(QStringLiteral("INPUT TOOLBAR touch begin target=%1")
                .arg(button->defaultAction()?button->defaultAction()->text():button->objectName()));
            event->accept();
            return true;
        }
        if (toolbarTouchId<0) return false;
        if (event->points().size()>1) toolbarTouchCancelled=true;
        const QEventPoint* tracked=nullptr;
        for (const QEventPoint& p:event->points())
            if (p.id()==toolbarTouchId) {tracked=&p;break;}
        const bool finishing=event->type()==QEvent::TouchEnd ||
            (tracked && tracked->state()==QEventPoint::State::Released);
        if (tracked && (tracked->globalPosition()-toolbarTouchStart).manhattanLength() >
            std::max(18,QApplication::startDragDistance()*2))
            toolbarTouchCancelled=true;
        if (finishing) {
            QPointer<QToolButton> target=toolbarTouchButton;
            const QPointF release=tracked ? tracked->globalPosition():toolbarTouchStart;
            const bool sameButton=target && toolbarAtFinger(release)==target;
            const bool click=!toolbarTouchCancelled && sameButton;
            toolbarTouchButton.clear();toolbarTouchId=-1;toolbarTouchCancelled=false;
            if (click) clickFreeCADToolButton(target.data(),release);
        }
        event->accept();
        return true;
    }
    static bool isCadView(QGraphicsView* view) {
        return view->inherits("Gui::View3DInventorViewer") ||
               view->inherits("SIM::Coin3D::Quarter::QuarterWidget") ||
               view->property("drnasCadViewport").toBool();
    }
    QWidget* viewportFor(QWidget* widget) const {
        if (!cad || !widget || !(widget==cad || cad->isAncestorOf(widget))) return nullptr;
        for (auto* view:cad->findChildren<QGraphicsView*>()) {
            if (!isCadView(view)) continue;
            QWidget* viewport=view->viewport();
            if (view->isVisible() && viewport &&
                (widget==viewport || viewport->isAncestorOf(widget))) return viewport;
        }
        return nullptr;
    }
    void resetGesture() {
        precisionTimer.stop(); cameraTimer.stop();
        orbitDelta=QPointF(); panDelta=QPointF(); zoomLog=0;
        gesture=Gesture::Idle; fingerId=-1; multiDistance=0;
        touchOwner.clear(); cameraOwner.clear(); mark->hide();
        releaseAll();
    }
    bool editingTool() {
        if (!cad) return true;
        // Fail closed: if the bridge fails, a one-finger drag must never orbit
        // a sketch or task. Camera gestures with two fingers remain available.
        cad->setProperty("drnasTouchToolActive",true);
        command(QStringLiteral(
            "import FreeCADGui as Gui\n"
            "_drnas_doc=Gui.activeDocument()\n"
            "Gui.getMainWindow().setProperty('drnasTouchToolActive', bool("
            "(_drnas_doc and _drnas_doc.getInEdit()) or Gui.Control.activeDialog()))\n"));
        return toolMode->isChecked() || cad->property("drnasTouchToolActive").toBool();
    }
    void flushCamera() {
        if (!cameraOwner || !cameraOwner->isVisible() ||
            QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
            orbitDelta=QPointF(); panDelta=QPointF(); zoomLog=0; return;
        }
        const double h=std::max(1,cameraOwner->height());
        const QPointF orbit=std::exchange(orbitDelta,QPointF());
        const QPointF pan=std::exchange(panDelta,QPointF());
        const double z=std::exchange(zoomLog,0.0);
        const QPoint anchor=cameraOwner->mapFromGlobal(multiCenter.toPoint());
        const double ax=(anchor.x()-cameraOwner->width()/2.0)/h;
        const double ay=(anchor.y()-cameraOwner->height()/2.0)/h;
        if (orbit.isNull() && pan.isNull() && z==0) return;
        // Work in the existing Coin camera. No GL context, framebuffer, native
        // surface or synthetic multi-button mouse navigation is created here.
        command(QStringLiteral(R"PY(import FreeCAD as App, FreeCADGui as Gui, math
_drnas_doc=Gui.activeDocument()
if _drnas_doc:
    _v=_drnas_doc.activeView()
    _c=_v.getCameraNode()
    _r=_v.getCameraOrientation()
    _p=App.Vector(*_c.position.getValue().getValue())
    _f=max(1e-9,float(_c.focalDistance.getValue()))
    _center=_p-_r.multVec(App.Vector(0,0,_f))
    if (%1 or %2) and not (_drnas_doc.getInEdit() or Gui.Control.activeDialog()):
        _r=_r.multiply(App.Rotation(App.Vector(0,1,0),%1)).multiply(App.Rotation(App.Vector(1,0,0),%2))
        _v.setCameraOrientation(_r.Q)
        _p=_center+_r.multVec(App.Vector(0,0,_f))
    _ortho=hasattr(_c,'height')
    _span=float(_c.height.getValue()) if _ortho else 2*_f*math.tan(float(_c.heightAngle.getValue())/2)
    _p=_p+_r.multVec(App.Vector(-(%3)*_span,(%4)*_span,0))
    _scale=math.exp(-(%5))
    _p=_p+_r.multVec(App.Vector((%6)*_span*(1-_scale),-(%7)*_span*(1-_scale),0))
    if _ortho:
        _c.height=max(1e-9,min(1e15,_span*_scale))
    else:
        _new_f=max(1e-9,min(1e15,_f*_scale))
        _p=_p+_r.multVec(App.Vector(0,0,_new_f-_f))
        _c.focalDistance=_new_f
    _c.position=(_p.x,_p.y,_p.z)
    _v.redraw()
)PY").arg(-orbit.x()*180.0/h,0,'g',12).arg(-orbit.y()*180.0/h,0,'g',12)
            .arg(pan.x()/h,0,'g',12).arg(pan.y()/h,0,'g',12).arg(z,0,'g',12)
            .arg(ax,0,'g',12).arg(ay,0,'g',12));
    }
    void queueCamera(QPointF orbit,QPointF pan,double scaleLog=0) {
        cameraOwner=touchOwner;
        orbitDelta+=orbit; panDelta+=pan; zoomLog=std::clamp(zoomLog+scaleLog,-10.0,10.0);
        if (!cameraTimer.isActive()) cameraTimer.start();
    }
    void precisionPosition(const QPointF& finger) {
        if (!touchOwner) return;
        QPoint local=touchOwner->mapFromGlobal((finger+QPointF(0,-42)).toPoint());
        local.setX(std::clamp(local.x(),0,std::max(0,touchOwner->width()-1)));
        local.setY(std::clamp(local.y(),0,std::max(0,touchOwner->height()-1)));
        setPosition(touchOwner->mapToGlobal(local));
        mark->move(cursor.toPoint()-QPoint(12,12)); mark->show(); mark->raise();
        mouseReceiver=touchOwner;
        sendMouse(QEvent::MouseMove,Qt::NoButton);
    }
    void startTouch(QWidget* owner,const QEventPoint& point) {
        resetGesture(); touchOwner=owner; lastViewport=owner;
        touchStart=touchLast=point.globalPosition(); fingerId=point.id();
        setPosition(touchStart); keyReceiver=owner;
        owner->setFocus(Qt::MouseFocusReason);
        toolAtStart=editingTool();
        if (multiSelect->isChecked()) modifiers|=Qt::ControlModifier;
        gesture=Gesture::Pending;
        precisionTimer.start();
        DrnasDiagnostics::note(QStringLiteral("INPUT touch begin owner=%1 logical=%2,%3 dpr=%4 tool=%5")
            .arg(QString::fromLatin1(owner->metaObject()->className()))
            .arg(owner->mapFromGlobal(touchStart.toPoint()).x())
            .arg(owner->mapFromGlobal(touchStart.toPoint()).y())
            .arg(owner->devicePixelRatioF()).arg(toolAtStart));
    }
    void handleTouch(QWidget* owner,QTouchEvent* event) {
        QList<QEventPoint> live;
        for (const auto& point:event->points())
            if (point.state()!=QEventPoint::State::Released) live.append(point);
        if (event->type()==QEvent::TouchBegin && !live.isEmpty()) startTouch(owner,live.first());
        if (!touchOwner) return;
        if (live.size()>=2) {
            precisionTimer.stop(); mark->hide();
            const QPointF center=(live[0].globalPosition()+live[1].globalPosition())/2;
            const double distance=QLineF(live[0].globalPosition(),live[1].globalPosition()).length();
            if (gesture!=Gesture::Multi) {
                releaseAll(); flushCamera(); gesture=Gesture::Multi;
                DrnasDiagnostics::note(QStringLiteral("INPUT two-finger pan/pinch"));
            } else {
                const double ratio=(multiDistance>4 && distance>4) ? distance/multiDistance : 1.0;
                queueCamera(QPointF(),center-multiCenter,std::log(ratio));
            }
            multiCenter=center; multiDistance=distance;
            return;
        }
        // A two-finger gesture stays navigation until EVERY finger is lifted.
        // Never convert its remaining finger into a click or sketch point.
        if (gesture==Gesture::Multi) {
            if (!live.isEmpty()) return;
            flushCamera(); resetGesture(); return;
        }
        const QEventPoint* tracked=nullptr;
        for (const auto& point:event->points()) if (point.id()==fingerId) {tracked=&point;break;}
        if (!tracked) return;
        const QPointF global=tracked->globalPosition();
        const bool released=tracked->state()==QEventPoint::State::Released;
        if (gesture==Gesture::Precision) precisionPosition(global);
        else if ((global-touchStart).manhattanLength()>QApplication::startDragDistance() ||
                 gesture==Gesture::Orbit || gesture==Gesture::Tool) {
            precisionTimer.stop();
            if (gesture==Gesture::Pending) {
                gesture=toolAtStart ? Gesture::Tool : Gesture::Orbit;
                if (gesture==Gesture::Tool) {
                    setPosition(touchStart); mouseReceiver=touchOwner;
                    sendMouse(QEvent::MouseMove,Qt::NoButton);
                    mouseButton(Qt::LeftButton,true);
                }
            }
            if (gesture==Gesture::Orbit) queueCamera(global-touchLast,QPointF());
            else if (gesture==Gesture::Tool) {
                setPosition(global); sendMouse(QEvent::MouseMove,Qt::NoButton);
            }
        }
        touchLast=global;
        if (released) {
            precisionTimer.stop();
            if (gesture==Gesture::Pending || gesture==Gesture::Precision) {
                if (gesture==Gesture::Pending) setPosition(global);
                mouseReceiver=touchOwner;
                sendMouse(QEvent::MouseMove,Qt::NoButton);
                mouseButton(Qt::LeftButton,true); mouseButton(Qt::LeftButton,false);
            }
            flushCamera(); resetGesture();
        }
    }
    void cancelInput() {
        // Android Back closes the native Qt popup first, then falls back to
        // FreeCAD's normal Escape semantics for active sketch/task commands.
        resetGesture();
        if (QWidget* popup=QApplication::activePopupWidget()) {
            popup->close();
            DrnasDiagnostics::note(QStringLiteral("INPUT: popup dismissed by Esc/Back"));
            return;
        }
        sendKey(Qt::Key_Escape);
    }
    void openContextMenu() {
        // No relative cursor or touchpad: invoke the original FreeCAD RMB
        // exactly at the last finger position (or the viewport center).
        if (QApplication::activeModalWidget()) return;
        if (QWidget* popup=QApplication::activePopupWidget()) { popup->close(); return; }
        QWidget* target=lastViewport.data();
        if (!target || !target->isVisible()) return;
        if (!target->rect().contains(target->mapFromGlobal(globalPoint())))
            setPosition(target->mapToGlobal(target->rect().center()));
        resetGesture();
        mouseReceiver=target;
        mouseButton(Qt::RightButton,true);
        mouseButton(Qt::RightButton,false);
        DrnasDiagnostics::note(QStringLiteral("INPUT: direct RMB at logical viewport position"));
    }
    void zoomStep() {
        if (QApplication::activeModalWidget()) { queuedZoom=0; zoomTimer.stop(); return; }
        QWidget* target=lastViewport;
        if (!target && cad) for (auto* view : cad->findChildren<QGraphicsView*>()) {
            if (isCadView(view) && view->isVisible() && view->width()>50 && view->height()>50) { target=view->viewport(); break; }
        }
        if (!target) { queuedZoom=0; zoomTimer.stop(); return; }
        const int delta=std::clamp(queuedZoom,-60,60);
        queuedZoom-=delta;
        QPoint local=target->mapFromGlobal(globalPoint());
        if (!target->rect().contains(local)) local=target->rect().center();
        QWheelEvent event(QPointF(local),QPointF(target->mapToGlobal(local)),QPoint(),QPoint(0,delta),
                          Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        deliverEvent(target,&event);
        if (!queuedZoom) zoomTimer.stop();
    }
    void queueZoom(int delta) {
        queuedZoom=std::clamp(queuedZoom+delta,-1200,1200);
        if (queuedZoom && !zoomTimer.isActive()) zoomTimer.start();
    }
    void savePosition() {
        QSettings s;
        s.setValue(QStringLiteral("touch/panelX"),double(panel->x())/std::max(1,host->width()));
        s.setValue(QStringLiteral("touch/panelY"),double(panel->y())/std::max(1,host->height()));
    }
    void constrain() {
        panel->setFixedWidth(std::max(80,std::min(340,host->width()-12)));
        panel->setFixedHeight(std::max(60,std::min(350,host->height()-80)));
        panel->move(std::clamp(panel->x(),0,std::max(0,host->width()-panel->width())),
                    std::clamp(panel->y(),0,std::max(0,host->height()-panel->height())));
        const int bottom=std::max(0,host->height()-90);
        launcher->setGeometry(std::max(0,host->width()-77),bottom,72,34);
        cancelButton->setGeometry(std::max(0,host->width()-160),bottom,78,34);
        contextButton->setGeometry(std::max(0,host->width()-231),bottom,66,34);
        panel->raise(); launcher->raise(); cancelButton->raise(); contextButton->raise();
    }
    void repairWindows() {
        if (!cad || repairing) return;
        repairing=true;
        for (auto* area : cad->findChildren<QMdiArea*>()) {
            // Desktop cascade positions are relative to the MDI viewport,
            // not to the screen. New documents must fill this viewport.
            if (!area->property("drnasMobileMdi").toBool()) {
                area->setProperty("drnasMobileMdi",true);
                area->setOption(QMdiArea::DontMaximizeSubWindowOnActivation,false);
                connect(area,&QMdiArea::subWindowActivated,this,[this](QMdiSubWindow*){resetGesture();lastViewport.clear();scheduleRepair();});
            }
            const QSize available=area->viewport()->size();
            if (available.width()<32 || available.height()<32) continue;
            const QRect bounds(QPoint(),available);
            for (auto* sub : area->subWindowList()) {
                if (!sub->isVisible()) continue;
                if (sub==area->activeSubWindow() || !sub->property("drnasMobilePlaced").toBool()) {
                    sub->setProperty("drnasMobilePlaced",true);
                    sub->setMinimumSize(0,0);
                    if (!sub->isMaximized()) {
                        sub->move(0,0);
                        sub->resize(available);
                        sub->showMaximized();
                        DrnasDiagnostics::note(QStringLiteral("MDI: document maximized in viewport %1x%2")
                            .arg(available.width()).arg(available.height()));
                    }
                    continue;
                }
                if (bounds.contains(sub->geometry())) continue;
                sub->setMinimumSize(0,0);
                const QSize size=sub->size().boundedTo(available);
                sub->resize(size);
                sub->move(std::clamp(sub->x(),0,std::max(0,available.width()-sub->width())),
                          std::clamp(sub->y(),0,std::max(0,available.height()-sub->height())));
            }
        }
        for (auto* view:cad->findChildren<QGraphicsView*>())
            if (isCadView(view)) view->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
        prepareFreeCADToolbars();
        const QRect bounds(host->mapToGlobal(QPoint()),host->size());
        for (QWidget* top : QApplication::topLevelWidgets()) {
            auto* dialog=qobject_cast<QDialog*>(top);
            if (!dialog || !dialog->isVisible()) continue;
            QWidget* owner=dialog->parentWidget();
            if (!owner || (owner!=host && !host->isAncestorOf(owner))) continue;
            if (bounds.contains(dialog->frameGeometry())) continue;
            dialog->setMinimumSize(0,0); dialog->setMaximumSize(bounds.size());
            dialog->resize(dialog->size().boundedTo(bounds.size()));
            dialog->move(std::clamp(dialog->x(),bounds.left(),std::max(bounds.left(),bounds.right()-dialog->width()+1)),
                         std::clamp(dialog->y(),bounds.top(),std::max(bounds.top(),bounds.bottom()-dialog->height()+1)));
        }
        repairing=false;
    }
    void scheduleRepair() {
        if (repairing || repairQueued) return;
        repairQueued=true;
        QTimer::singleShot(0,this,[this]{repairQueued=false; repairWindows(); constrain();});
    }
protected:
    bool eventFilter(QObject* obj,QEvent* event) override {
        if (obj==host && event->type()==QEvent::Destroy) {
            qApp->removeEventFilter(this); return false;
        }
        if (injectedEvents.contains(event)) return false;
        if (event->type()==QEvent::KeyPress) {
            auto* key=static_cast<QKeyEvent*>(event);
            if (key->key()==Qt::Key_Back ||
                (key->key()==Qt::Key_Escape && QApplication::activePopupWidget())) {
                cancelInput();
                event->accept(); return true;
            }
        }
        if (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove ||
            event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseButtonDblClick) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            // Our posted mouse events (and their propagated copies) already
            // contain mapped viewport coordinates; never feed them back into
            // the physical-finger tracking path.
            if (!event->spontaneous() && mouse->source()==Qt::MouseEventNotSynthesized) return false;
        }
        auto* inputWidget=qobject_cast<QWidget*>(obj);
        const bool touchEvent=event->type()==QEvent::TouchBegin || event->type()==QEvent::TouchUpdate ||
                              event->type()==QEvent::TouchEnd;
        if (event->type()==QEvent::ApplicationDeactivate ||
            (obj==host && (event->type()==QEvent::Hide || event->type()==QEvent::WindowDeactivate)) ||
            (obj==touchOwner && (event->type()==QEvent::Hide || event->type()==QEvent::FocusOut)) ||
            (inputWidget && (inputWidget->windowType()==Qt::Popup || qobject_cast<QDialog*>(inputWidget)) &&
             event->type()==QEvent::Show)) resetGesture();
        if (event->type()==QEvent::TouchCancel && toolbarTouchId>=0) {
            toolbarTouchButton.clear();toolbarTouchId=-1;toolbarTouchCancelled=false;
            event->accept();return true;
        }
        if (event->type()==QEvent::TouchCancel && touchOwner) {
            resetGesture(); event->accept(); return true;
        }
        // Toolbars use direct absolute-position tap routing, separately from
        // CAD gestures. Even if Android delivers a touch to the embedded main
        // window rather than its QToolButton, hit-test the original toolbar.
        if (touchEvent && handleFreeCADToolbarTouch(static_cast<QTouchEvent*>(event)))
            return true;
        // UI widgets keep normal Qt hit testing, focus, scrolling and modality.
        // Only a sequence which begins inside a CAD viewport belongs to us.
        const bool mouseEvent=event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove ||
                              event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseButtonDblClick;
        if ((touchEvent || mouseEvent) && !QApplication::activeModalWidget() && !QApplication::activePopupWidget()) {
            QWidget* viewport=viewportFor(inputWidget);
            if (touchEvent && viewport && (!touchOwner || viewport==touchOwner)) {
                handleTouch(touchOwner ? touchOwner.data() : viewport,static_cast<QTouchEvent*>(event));
                event->accept(); return true;
            }
            if (viewport && (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove ||
                event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseButtonDblClick)) {
                auto* mouse=static_cast<QMouseEvent*>(event);
                if (mouse->source()!=Qt::MouseEventNotSynthesized) {
                    event->accept(); return true; // native touch already handled above
                }
                lastViewport=viewport;
            }
        }
        if (obj==host && event->type()==QEvent::Resize) {
            resetGesture();
            QSettings s;
            panel->move(int(s.value(QStringLiteral("touch/panelX"),0.02).toDouble()*host->width()),
                        int(s.value(QStringLiteral("touch/panelY"),0.15).toDouble()*host->height()));
            cursor=QPointF(host->width()/2.0,host->height()/2.0); scheduleRepair();
        }
        if (event->type()==QEvent::Show &&
            (qobject_cast<QToolBar*>(obj) || qobject_cast<QToolButton*>(obj)))
            prepareFreeCADToolbars();
        if ((event->type()==QEvent::Show || event->type()==QEvent::Move || event->type()==QEvent::Resize) &&
            (qobject_cast<QMdiSubWindow*>(obj) || qobject_cast<QDialog*>(obj) || qobject_cast<QMdiArea*>(obj))) scheduleRepair();
        if (obj==panel && event->type()==QEvent::Hide) { releaseAll(); savePosition(); }
        if (obj==grip) {
            if (event->type()==QEvent::MouseButtonPress) {
                auto* e=static_cast<QMouseEvent*>(event);
                if (e->button()==Qt::LeftButton) { dragging=true; dragOffset=e->globalPosition().toPoint()-panel->mapToGlobal(QPoint()); return true; }
            }
            if (event->type()==QEvent::MouseMove && dragging) {
                auto* e=static_cast<QMouseEvent*>(event);
                panel->move(host->mapFromGlobal(e->globalPosition().toPoint()-dragOffset)); constrain(); return true;
            }
            if (event->type()==QEvent::MouseButtonRelease && dragging) { dragging=false; savePosition(); return true; }
        }
        QWidget* widget=qobject_cast<QWidget*>(obj);
        if (cad && widget && (widget==cad || cad->isAncestorOf(widget))) {
            if (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove) {
                auto* e=static_cast<QMouseEvent*>(event);
                cursor=host->mapFromGlobal(e->globalPosition().toPoint());
                keyReceiver=widget;

            }
        }
        return false;
    }
public:
    DrnasTouchInput(QMainWindow* shell,QMainWindow* freecad,std::function<void(const QString&)> run)
        : QObject(shell),host(shell),cad(freecad),panel(new QWidget(shell)),zoom(nullptr),command(std::move(run)) {
        panel->setObjectName(QStringLiteral("drnasTouchInputPanel"));
        panel->setStyleSheet(QStringLiteral(
            "QWidget{background:#182d42;color:#e9f2fa;font-size:12px;}"
            "QPushButton{min-height:30px;background:#315775;border:1px solid #52718d;border-radius:4px;}"
            "QPushButton:checked,QPushButton:pressed{background:#278caa;}"));
        auto* outer=new QVBoxLayout(panel); outer->setContentsMargins(5,4,5,5); outer->setSpacing(3);
        auto* heading=new QHBoxLayout;
        grip=new QLabel(QStringLiteral("Dodir i geste · povuci"),panel); grip->setMinimumHeight(30);
        auto* close=new QPushButton(QStringLiteral("×"),panel); close->setFixedWidth(34);
        close->setFocusPolicy(Qt::NoFocus);
        heading->addWidget(grip,1); heading->addWidget(close); outer->addLayout(heading);
        auto* scroll=new QScrollArea(panel); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content=new QWidget(scroll); auto* body=new QVBoxLayout(content);
        body->setContentsMargins(0,0,0,0); body->setSpacing(3); scroll->setWidget(content); outer->addWidget(scroll);
        auto button=[content](const QString& text) { auto* b=new QPushButton(text,content); b->setFocusPolicy(Qt::NoFocus); return b; };
        auto* modes=new QHBoxLayout;
        toolMode=button(QStringLiteral("Alat / povlačenje")); toolMode->setCheckable(true);
        toolMode->setObjectName(QStringLiteral("drnasTouchToolMode"));
        multiSelect=button(QStringLiteral("Višestruki odabir")); multiSelect->setCheckable(true);
        multiSelect->setObjectName(QStringLiteral("drnasTouchMultiSelect"));
        toolMode->setToolTip(QStringLiteral("Jedan prst povlači alat; dva prsta pomiču i zumiraju kameru"));
        modes->addWidget(toolMode); modes->addWidget(multiSelect); body->addLayout(modes);
        connect(toolMode,&QPushButton::toggled,this,[this]{resetGesture();});
        connect(multiSelect,&QPushButton::toggled,this,[this](bool on){
            modifier(Qt::ControlModifier,Qt::Key_Control,on);
        });

        auto* help=new QLabel(QStringLiteral("Dodir: odabir · povuci: orbita\n"
            "Dva prsta: pomak i zoom\nZadrži prst: precizni križić\n"
            "Skica/zadatak: jedan prst koristi alat"),content);
        help->setWordWrap(true); body->addWidget(help);
        auto* zoomRow=new QHBoxLayout;
        auto* minus=button(QStringLiteral("−")); minus->setFixedWidth(34);
        auto* plus=button(QStringLiteral("+")); plus->setFixedWidth(34);
        zoom=new QSlider(Qt::Horizontal,content); zoom->setRange(-100,100); zoom->setValue(0); zoom->setMinimumHeight(32);
        zoom->setFocusPolicy(Qt::NoFocus); zoom->setToolTip(QStringLiteral("Zoom — vraća se u sredinu"));
        zoomRow->addWidget(minus); zoomRow->addWidget(zoom,1); zoomRow->addWidget(plus); body->addLayout(zoomRow);
        zoomTimer.setInterval(16);
        connect(&zoomTimer,&QTimer::timeout,this,[this]{zoomStep();});
        connect(zoom,&QSlider::sliderMoved,this,[this](int value){queueZoom((value-previousZoom)*12); previousZoom=value;});
        connect(zoom,&QSlider::sliderReleased,this,[this]{QSignalBlocker b(zoom); zoom->setValue(0); previousZoom=0;});
        connect(minus,&QPushButton::clicked,this,[this]{queueZoom(-120);});
        connect(plus,&QPushButton::clicked,this,[this]{queueZoom(120);});
        auto* mods=new QHBoxLayout;
        struct Modifier {const char* name; Qt::KeyboardModifier flag; int key;};
        for (auto m : {Modifier{"Ctrl",Qt::ControlModifier,Qt::Key_Control},{"Shift",Qt::ShiftModifier,Qt::Key_Shift},{"Alt",Qt::AltModifier,Qt::Key_Alt}}) {
            auto* b=button(QString::fromLatin1(m.name)); b->setCheckable(true); modifierKeys.append(b); mods->addWidget(b);
            connect(b,&QPushButton::toggled,this,[this,m](bool down){modifier(m.flag,m.key,down);});
        }
        for (auto pair : {std::pair<const char*,int>{"Esc",Qt::Key_Escape},{"Enter",Qt::Key_Return}}) {
            auto* b=button(QString::fromLatin1(pair.first)); mods->addWidget(b);
            connect(b,&QPushButton::clicked,this,[this,pair]{if(pair.second==Qt::Key_Escape)cancelInput();else sendKey(pair.second);});
        }
        body->addLayout(mods);
        auto* keys=new QHBoxLayout;
        for (auto pair : {std::pair<const char*,int>{"Tab",Qt::Key_Tab},{"Del",Qt::Key_Delete},{"Space",Qt::Key_Space},{"Undo",Qt::Key_Z}}) {
            auto* b=button(QString::fromLatin1(pair.first)); keys->addWidget(b);
            connect(b,&QPushButton::clicked,this,[this,pair]{sendKey(pair.second,pair.second==Qt::Key_Space?QStringLiteral(" "):QString(),pair.second==Qt::Key_Z?Qt::ControlModifier:Qt::NoModifier);});
        }
        auto* fit=button(QStringLiteral("Fit")); keys->addWidget(fit);
        connect(fit,&QPushButton::clicked,this,[this]{command(QStringLiteral("import FreeCADGui as Gui\nif Gui.activeDocument(): Gui.activeDocument().activeView().fitAll()\n"));});
        body->addLayout(keys);
        auto* footer=new QHBoxLayout;
        auto* recover=button(QStringLiteral("Vrati prozore")); footer->addWidget(recover);
        connect(recover,&QPushButton::clicked,this,[this]{releaseAll();repairWindows();constrain();savePosition();DrnasDiagnostics::note(QStringLiteral("INPUT: visible windows recovered"));});
        body->addLayout(footer);
        QSettings settings;
        connect(close,&QPushButton::clicked,panel,&QWidget::hide);
        launcher=new QPushButton(QStringLiteral("Geste"),host); launcher->setObjectName(QStringLiteral("drnasTouchInputLauncher"));
        launcher->setFocusPolicy(Qt::NoFocus); launcher->setStyleSheet(QStringLiteral("background:#315775;color:white;border-radius:5px;"));
        connect(launcher,&QPushButton::clicked,this,[this]{panel->setVisible(!panel->isVisible());constrain();});
        cancelButton=new QPushButton(QStringLiteral("Esc"),host);
        cancelButton->setObjectName(QStringLiteral("drnasTouchCancel"));
        cancelButton->setFocusPolicy(Qt::NoFocus);
        cancelButton->setStyleSheet(launcher->styleSheet());
        connect(cancelButton,&QPushButton::clicked,this,[this]{cancelInput();});
        contextButton=new QPushButton(QStringLiteral("RMB"),host);
        contextButton->setObjectName(QStringLiteral("drnasTouchContext"));
        contextButton->setFocusPolicy(Qt::NoFocus);
        contextButton->setToolTip(QStringLiteral("Desni klik na zadnjem odabiru u 3D prikazu"));
        contextButton->setStyleSheet(launcher->styleSheet());
        connect(contextButton,&QPushButton::clicked,this,[this]{openContextMenu();});
        mark=new DrnasPointerMark(host); mark->hide();
        precisionTimer.setSingleShot(true); precisionTimer.setInterval(500); precisionTimer.setTimerType(Qt::PreciseTimer);
        connect(&precisionTimer,&QTimer::timeout,this,[this]{
            if (gesture==Gesture::Pending && touchOwner &&
                !QApplication::activeModalWidget() && !QApplication::activePopupWidget()) {
                gesture=Gesture::Precision; precisionPosition(touchLast);
            }
        });
        cameraTimer.setSingleShot(true); cameraTimer.setInterval(16);
        connect(&cameraTimer,&QTimer::timeout,this,[this]{flushCamera();});
        cursor=QPointF(host->width()/2.0,host->height()/2.0);
        panel->move(int(settings.value(QStringLiteral("touch/panelX"),0.02).toDouble()*host->width()),
                    int(settings.value(QStringLiteral("touch/panelY"),0.15).toDouble()*host->height()));
        // These child widgets were installed by DrnasFloatingCommands before
        // this global input filter. Cache them to route Tools taps directly.
        commandsPanel=host->findChild<QWidget*>(
            QStringLiteral("drnasFloatingCommands"),Qt::FindDirectChildrenOnly);
        commandsLauncher=host->findChild<QWidget*>(
            QStringLiteral("drnasFloatingCommandsLauncher"),Qt::FindDirectChildrenOnly);
        DrnasDiagnostics::note(QStringLiteral("INPUT: Tools direct touch routes panel=%1 launcher=%2")
            .arg(bool(commandsPanel)).arg(bool(commandsLauncher)));
        // Fix a useful initial scroll area height without reserving CAD layout space.
        scroll->setMinimumHeight(0);
        constrain(); panel->hide(); launcher->show();
        qApp->installEventFilter(this);
        scheduleRepair();
        DrnasDiagnostics::note(QStringLiteral("INPUT: direct touch; viewport orbit/pan/pinch; relative mouse removed"));
    }
};
