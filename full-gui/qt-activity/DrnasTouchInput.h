// SPDX-License-Identifier: LGPL-2.1-or-later
// Qt adaptation of DRNAS Blender's draggable mouse/key groups and spring zoom.
// Uses existing widgets and logical coordinates; creates no native/GL surface.
#pragma once
#include <algorithm>
#include <functional>
#include <utility>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QElapsedTimer>
#include <QFrame>
#include <QGraphicsView>
#include <QGridLayout>
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
#include <QVector>
#include <QVBoxLayout>
#include <QWheelEvent>
#include "StartupDiagnostics.h"
#include "DrnasDraggableLauncher.h"

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

class DrnasPointerPad final : public QLabel {
    QPointF last;
    bool active = false;
    int touchId = -1;
public:
    std::function<void(QPointF)> movePointer;
    explicit DrnasPointerPad(QWidget* parent) : QLabel(parent) {
        setText(QStringLiteral("Pomakni pokazivač ovdje"));
        setAlignment(Qt::AlignCenter);
        setMinimumHeight(62);
        setAttribute(Qt::WA_AcceptTouchEvents);
        setStyleSheet(QStringLiteral("background:#253c50;border:1px solid #6486a1;border-radius:6px;"));
    }
protected:
    bool event(QEvent* e) override {
        if (e->type()==QEvent::TouchCancel) { active=false; touchId=-1; e->accept(); return true; }
        if (e->type()==QEvent::TouchBegin || e->type()==QEvent::TouchUpdate || e->type()==QEvent::TouchEnd) {
            auto* touch=static_cast<QTouchEvent*>(e);
            for (const auto& point : touch->points()) {
                if (touchId<0 && point.state()==QEventPoint::State::Pressed) {
                    touchId=point.id(); last=point.position(); active=true;
                }
                if (point.id()!=touchId) continue;
                if (active && movePointer) movePointer(point.position()-last);
                last=point.position();
                if (point.state()==QEventPoint::State::Released) { active=false; touchId=-1; }
            }
            e->accept(); return true;
        }
        return QLabel::event(e);
    }
    void mousePressEvent(QMouseEvent* e) override { active=true; last=e->position(); e->accept(); }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (active && movePointer) movePointer(e->position()-last);
        last=e->position(); e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override { active=false; e->accept(); }
};

class DrnasTouchInput final : public QObject {
    QMainWindow* host;
    QPointer<QMainWindow> cad;
    QWidget* panel;
    QLabel* grip;
    QPushButton* launcher;
    DrnasDraggableLauncher* launcherDrag = nullptr;
    QPushButton* hold;
    DrnasPointerMark* mark;
    QPointer<DrnasPointerMark> dialogMark;
    // Remember each dialog's previous cursor so nested QDialog::exec() can
    // unwind back to its parent, then to the exact CAD workspace position.
    struct ModalCursorState {
        QPointer<QWidget> widget;
        QPointF returnCursor;
    };
    QVector<ModalCursorState> modalCursors;
    QSlider* zoom;
    QTimer zoomTimer;
    QPointer<QWidget> mouseReceiver;
    QPointer<QWidget> keyReceiver;
    QPointF cursor;
    QPoint dragOffset;
    QPointF relativeLast, relativeStart;
    QElapsedTimer relativeClock;
    bool relativeActive=false, relativeMoved=false;
    int relativeTouchId=-1;
    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    QList<QPushButton*> mouseKeys, modifierKeys;
    QSet<const QEvent*> injectedEvents;
    bool dragging=false, repairing=false, repairQueued=false;
    int queuedZoom=0, previousZoom=0, speed=100;
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
        // Resolve the actual button/viewport under the virtual cursor,
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
        // mouseButton(false) and leave our relative pointer holding LMB.
        QCoreApplication::postEvent(target.data(),new QMouseEvent(
            type,local,window,global,button,buttons,modifiers));
    }
    void mouseButton(Qt::MouseButton button, bool down) {
        if (down) {
            if (buttons.testFlag(button)) return;
            if (buttons==Qt::NoButton) {
                mouseReceiver=targetAtCursor();
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
    void syncModalPointer() {
        QWidget* active=QApplication::activeModalWidget();
        if (active && !active->isVisible()) return; // Qt has not finished Show yet.
        // Qt may destroy the top dialog before posting its Hide/Deactivate.
        while (!modalCursors.isEmpty() &&
               (!modalCursors.last().widget || !modalCursors.last().widget->isVisible())) {
            cursor=modalCursors.last().returnCursor;
            modalCursors.removeLast();
        }
        if (!active) {
            if (!modalCursors.isEmpty()) {
                cursor=modalCursors.first().returnCursor;
                modalCursors.clear();
                DrnasDiagnostics::note(QStringLiteral("INPUT modal pointer: restored CAD cursor"));
            }
            if (dialogMark) dialogMark->hide();
            showPointer();
            return;
        }
        int existing=-1;
        for (int i=0;i<modalCursors.size();++i) {
            if (modalCursors[i].widget==active) { existing=i; break; }
        }
        if (existing>=0) {
            // A child exec() finished: restore the parent's cursor, not the
            // workspace cursor, until the outermost modal also closes.
            while (modalCursors.size()>existing+1) {
                cursor=modalCursors.last().returnCursor;
                modalCursors.removeLast();
            }
        } else {
            // Release the previous mouse grab ONCE at a modal transition.
            // Never synthesize releases between a button's press and release.
            relativeActive=false; relativeTouchId=-1;
            if (buttons!=Qt::NoButton || modifiers!=Qt::NoModifier) releaseAll();
            modalCursors.append({QPointer<QWidget>(active),cursor});
            if (!active->rect().contains(active->mapFromGlobal(globalPoint()))) {
                cursor=host->mapFromGlobal(active->mapToGlobal(active->rect().center()));
            }
            DrnasDiagnostics::note(QStringLiteral("INPUT modal pointer: attach %1 depth=%2")
                .arg(QString::fromLatin1(active->metaObject()->className()))
                .arg(modalCursors.size()));
        }
        showPointer();
    }
    void showPointer() {
        const QRect r=host->rect();
        if (r.isEmpty()) return;
        cursor.setX(std::clamp(cursor.x(),double(r.left()),double(r.right())));
        cursor.setY(std::clamp(cursor.y(),double(r.top()),double(r.bottom())));
        QWidget* top=QApplication::activePopupWidget();
        if (!top) top=QApplication::activeModalWidget();
        // A popup clips its children. Draw on the host while the cursor is
        // outside it, instead of making a moving cursor appear frozen/hidden.
        if (top && !top->rect().contains(top->mapFromGlobal(globalPoint()))) top=nullptr;
        if (top) {
            mark->hide();
            if (!dialogMark || dialogMark->parentWidget()!=top) {
                if (dialogMark) dialogMark->hide();
                dialogMark=static_cast<DrnasPointerMark*>(top->findChild<QWidget*>(QStringLiteral("drnasModalPointer"),Qt::FindDirectChildrenOnly));
                if (!dialogMark) {
                    dialogMark=new DrnasPointerMark(top);
                    dialogMark->setObjectName(QStringLiteral("drnasModalPointer"));
                }
            }
            dialogMark->move(top->mapFromGlobal(globalPoint())-QPoint(12,12));
            dialogMark->show(); dialogMark->raise();
        } else {
            if (dialogMark) dialogMark->hide();
            mark->move(cursor.toPoint()-QPoint(12,12)); mark->show(); mark->raise();
        }
    }
    void movePointer(QPointF delta) {
        cursor+=delta*(double(speed)/100.0);
        if (QWidget* modal=QApplication::activeModalWidget()) {
            // Keep the virtual pointer in the actual modal hit-test area.
            // A native Android dialog is a separate top-level QWindow, so
            // painting a marker on the shell OUTSIDE its bounds is misleading.
            const QRect inside=modal->rect().adjusted(2,2,-2,-2);
            if (inside.isValid()) {
                const QPoint a=host->mapFromGlobal(modal->mapToGlobal(inside.topLeft()));
                const QPoint b=host->mapFromGlobal(modal->mapToGlobal(inside.bottomRight()));
                cursor.setX(std::clamp(cursor.x(),double(std::min(a.x(),b.x())),double(std::max(a.x(),b.x()))));
                cursor.setY(std::clamp(cursor.y(),double(std::min(a.y(),b.y())),double(std::max(a.y(),b.y()))));
            }
        }
        showPointer(); sendMouse(QEvent::MouseMove,Qt::NoButton);
    }
    void beginRelative(const QPointF& p) {
        relativeActive=true; relativeMoved=false;
        relativeLast=relativeStart=p; relativeClock.start();
        showPointer();
    }
    void updateRelative(const QPointF& p) {
        if (!relativeActive) return;
        if ((p-relativeStart).manhattanLength()>QApplication::startDragDistance()) relativeMoved=true;
        movePointer(p-relativeLast); relativeLast=p;
    }
    void endRelative(const QPointF& p) {
        if (!relativeActive) return;
        updateRelative(p); relativeActive=false;
        if (!relativeMoved && relativeClock.elapsed()<350 && buttons==Qt::NoButton) {
            mouseButton(Qt::LeftButton,true);
            mouseButton(Qt::LeftButton,false);
        }
    }
    void zoomStep() {
        if (QApplication::activeModalWidget()) { queuedZoom=0; zoomTimer.stop(); return; }
        QWidget* target=nullptr;
        if (cad) for (auto* view : cad->findChildren<QGraphicsView*>()) {
            if (view->isVisible() && view->width()>50 && view->height()>50) { target=view->viewport(); break; }
        }
        if (!target) { queuedZoom=0; zoomTimer.stop(); return; }
        const int delta=std::clamp(queuedZoom,-60,60);
        queuedZoom-=delta;
        QPoint local=target->mapFromGlobal(globalPoint());
        if (!target->rect().contains(local)) local=target->rect().center();
        QWheelEvent event(QPointF(local),QPointF(target->mapToGlobal(local)),QPoint(),QPoint(0,delta),
                          Qt::NoButton,modifiers,Qt::NoScrollPhase,false);
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
        if (launcherDrag) launcherDrag->place();
        panel->raise(); launcher->raise();
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
                connect(area,&QMdiArea::subWindowActivated,this,[this](QMdiSubWindow*){scheduleRepair();});
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
        if (injectedEvents.contains(event)) return false;
        if (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove ||
            event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseButtonDblClick) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            // Our posted mouse events (and their propagated copies) already
            // contain virtual cursor coordinates; never feed them back into
            // the physical-finger tracking path.
            if (!event->spontaneous() && mouse->source()==Qt::MouseEventNotSynthesized) return false;
        }
        if (auto* popupWidget=qobject_cast<QWidget*>(obj)) {
            if ((popupWidget->windowType()==Qt::Popup) &&
                (event->type()==QEvent::Show || event->type()==QEvent::Hide)) {
                DrnasDiagnostics::note(QStringLiteral("INPUT popup %1 class=%2 buttons=%3")
                    .arg(event->type()==QEvent::Show ? QStringLiteral("show") : QStringLiteral("hide"))
                    .arg(QString::fromLatin1(popupWidget->metaObject()->className())).arg(int(buttons)));
                QTimer::singleShot(0,this,[this]{showPointer();});
            }
            if (qobject_cast<QDialog*>(popupWidget) &&
                (event->type()==QEvent::Show || event->type()==QEvent::Hide ||
                 event->type()==QEvent::Close || event->type()==QEvent::WindowActivate)) {
                // Show/Hide can precede Qt's activeModalWidget update.
                QTimer::singleShot(0,this,[this]{syncModalPointer();});
            }
        }
        auto* inputWidget=qobject_cast<QWidget*>(obj);
        QWidget* modal=QApplication::activeModalWidget();
        QWidget* popup=QApplication::activePopupWidget();
        if (modal && inputWidget &&
            (inputWidget==modal || modal->isAncestorOf(inputWidget))) {
            // Use the SAME relative touch -> cursor -> tap as the main screen.
            // Old "modal direct mode" prevented dragging the cursor in dialogs.
            // Only transition once; a nested event loop can otherwise release
            // a QDialogButtonBox button between its press and release.
            if (event->type()==QEvent::TouchBegin ||
                event->type()==QEvent::MouseButtonPress) syncModalPointer();
        }
        const bool inApp=inputWidget && (inputWidget==host || host->isAncestorOf(inputWidget) ||
            (modal && (inputWidget==modal || modal->isAncestorOf(inputWidget))) ||
            (popup && (inputWidget==popup || popup->isAncestorOf(inputWidget))));
        // Keep the compact mouse/key panel directly operable. Everywhere
        // else is a relative touchpad; lifting a finger never warps the cursor.
        const auto isDirectOverlay = [this](QWidget* widget) {
            for (QWidget* p = widget; p; p = p->parentWidget()) {
                if (p == panel || p == launcher) return true;
                const QString name = p->objectName();
                if (name == QStringLiteral("drnasFloatingCommandsLauncher") ||
                    name == QStringLiteral("drnasDebugLauncher") ||
                    name == QStringLiteral("drnasLiveLabButton") ||
                    name == QStringLiteral("drnasViewportSoloToggle") ||
                    name == QStringLiteral("drnasFloatingCommands") ||
                    name == QStringLiteral("drnasDebugPanel") ||
                    name == QStringLiteral("drnasLiveLabPanel")) return true;
            }
            return false;
        };
        const bool directControl = isDirectOverlay(inputWidget);
        if (inApp && !directControl) {
            if (event->type()==QEvent::TouchBegin || event->type()==QEvent::TouchUpdate || event->type()==QEvent::TouchEnd) {
                auto* touch=static_cast<QTouchEvent*>(event);
                for (const auto& point:touch->points()) {
                    if (relativeTouchId<0 && point.state()==QEventPoint::State::Pressed) {
                        relativeTouchId=point.id(); beginRelative(point.globalPosition());
                    }
                    if (point.id()!=relativeTouchId) continue;
                    if (point.state()==QEventPoint::State::Released) {
                        endRelative(point.globalPosition()); relativeTouchId=-1;
                    } else updateRelative(point.globalPosition());
                }
                event->accept(); return true;
            }
            if (event->type()==QEvent::TouchCancel) {
                relativeActive=false; relativeTouchId=-1; releaseAll(); event->accept(); return true;
            }
            if (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove ||
                event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseButtonDblClick) {
                auto* mouse=static_cast<QMouseEvent*>(event);
                if (mouse->source()!=Qt::MouseEventNotSynthesized) {
                    if (relativeTouchId<0) {
                        if (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonDblClick)
                            beginRelative(mouse->globalPosition());
                        else if (event->type()==QEvent::MouseButtonRelease) endRelative(mouse->globalPosition());
                        else updateRelative(mouse->globalPosition());
                    }
                    event->accept(); return true;
                }
            }
        }
        if (event->type()==QEvent::ApplicationDeactivate || (obj==host && event->type()==QEvent::Hide)) {
            relativeActive=false; relativeTouchId=-1; releaseAll();
            // Do not erase the modal return stack on a short-lived Android
            // focus switch while a dialog is still open.
            QTimer::singleShot(0,this,[this]{syncModalPointer();});
        }
        if (obj==host && event->type()==QEvent::Resize) {
            releaseAll();
            QSettings s;
            panel->move(int(s.value(QStringLiteral("touch/panelX"),0.02).toDouble()*host->width()),
                        int(s.value(QStringLiteral("touch/panelY"),0.15).toDouble()*host->height()));
            cursor=QPointF(host->width()/2.0,host->height()/2.0); scheduleRepair();
        }
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
                if (mark->isVisible()) showPointer();
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
        grip=new QLabel(QStringLiteral("Miš i tipke · povuci"),panel); grip->setMinimumHeight(30);
        auto* close=new QPushButton(QStringLiteral("×"),panel); close->setFixedWidth(34);
        close->setFocusPolicy(Qt::NoFocus);
        heading->addWidget(grip,1); heading->addWidget(close); outer->addLayout(heading);
        auto* scroll=new QScrollArea(panel); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content=new QWidget(scroll); auto* body=new QVBoxLayout(content);
        body->setContentsMargins(0,0,0,0); body->setSpacing(3); scroll->setWidget(content); outer->addWidget(scroll);
        auto button=[content](const QString& text) { auto* b=new QPushButton(text,content); b->setFocusPolicy(Qt::NoFocus); return b; };
        auto* mouseRow=new QHBoxLayout;
        for (auto pair : {std::pair<const char*,Qt::MouseButton>{"LMB",Qt::LeftButton},{"MMB",Qt::MiddleButton},{"RMB",Qt::RightButton}}) {
            auto* b=button(QString::fromLatin1(pair.first)); mouseKeys.append(b); mouseRow->addWidget(b);
            connect(b,&QPushButton::pressed,this,[this,pair]{ if (!hold->isChecked()) mouseButton(pair.second,true); });
            connect(b,&QPushButton::released,this,[this,pair]{ if (!hold->isChecked()) mouseButton(pair.second,false); });
            connect(b,&QPushButton::clicked,this,[this,pair](bool checked){ if (hold->isChecked()) mouseButton(pair.second,checked); });
        }
        hold=button(QStringLiteral("Drži")); hold->setCheckable(true); mouseRow->addWidget(hold);
        connect(hold,&QPushButton::toggled,this,[this](bool on){ releaseAll(); for (auto* b:mouseKeys) b->setCheckable(on); });
        auto* reset=button(QStringLiteral("Otpusti")); mouseRow->addWidget(reset);
        connect(reset,&QPushButton::clicked,this,[this]{releaseAll();}); body->addLayout(mouseRow);
        auto* pad=new DrnasPointerPad(content); body->addWidget(pad);
        pad->movePointer=[this](QPointF delta){movePointer(delta);};
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
            connect(b,&QPushButton::clicked,this,[this,pair]{if(pair.second==Qt::Key_Escape)releaseAll();sendKey(pair.second);});
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
        auto* gear=button(QStringLiteral("⚙")); gear->setFixedWidth(34); footer->addWidget(gear); body->addLayout(footer);
        auto* options=new QWidget(content); auto* optionLayout=new QVBoxLayout(options); optionLayout->setContentsMargins(0,0,0,0);
        optionLayout->addWidget(new QLabel(QStringLiteral("Brzina pokazivača"),options));
        auto* sensitivity=new QSlider(Qt::Horizontal,options); sensitivity->setRange(25,300);
        QSettings settings; speed=std::clamp(settings.value(QStringLiteral("touch/speed"),100).toInt(),25,300); sensitivity->setValue(speed); optionLayout->addWidget(sensitivity);
        connect(sensitivity,&QSlider::valueChanged,this,[this](int value){speed=value;QSettings s;s.setValue(QStringLiteral("touch/speed"),value);});
        body->addWidget(options); options->hide();
        connect(gear,&QPushButton::clicked,this,[this,options]{options->setVisible(!options->isVisible());constrain();});
        connect(close,&QPushButton::clicked,panel,&QWidget::hide);
        launcher=new QPushButton(QStringLiteral("Miš"),host); launcher->setObjectName(QStringLiteral("drnasTouchInputLauncher"));
        launcher->setFocusPolicy(Qt::NoFocus); launcher->setStyleSheet(QStringLiteral("background:#315775;color:white;border-radius:5px;"));
        launcher->setFixedSize(72,34);
        launcherDrag = new DrnasDraggableLauncher(host, launcher,
            QStringLiteral("drnasOverlay/mouse"), 108, true, this);
        connect(launcher,&QPushButton::clicked,this,[this]{panel->setVisible(!panel->isVisible());constrain();});
        mark=new DrnasPointerMark(host); mark->hide();
        cursor=QPointF(host->width()/2.0,host->height()/2.0);
        panel->move(int(settings.value(QStringLiteral("touch/panelX"),0.02).toDouble()*host->width()),
                    int(settings.value(QStringLiteral("touch/panelY"),0.15).toDouble()*host->height()));
        // Fix a useful initial scroll area height without reserving CAD layout space.
        scroll->setMinimumHeight(0);
        constrain(); panel->hide(); launcher->show();
        qApp->installEventFilter(this);
        scheduleRepair();
        showPointer();
        DrnasDiagnostics::note(QStringLiteral("INPUT: relative pointer on workspace and Qt modal dialogs; nested cursor restore enabled"));
    }
};
