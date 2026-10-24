// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <algorithm>
#include <cstdlib>
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
        move(std::clamp(x(),0,std::max(0,host->width()-width())),std::clamp(y(),0,std::max(0,host->height()-height())));
        if(!QApplication::activeModalWidget() && !QApplication::activePopupWidget())raise();
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
    bool twoFingerPan=false,suppressAfterPan=false;
    QPointF panLast;
    int touchId=-1,zoomDeflection=0,zoomTicks=0;
    // This FreeCAD desktop modal is embedded in the existing QtActivity
    // QWidget tree: no competing Android QWindow or native focus loop.
    QPointer<QDialog> embeddedDialog;
    QPointer<QWidget> modalShield;
    QPointer<QWidget> originalDialogParent;
    Qt::WindowFlags originalDialogFlags{};
    Qt::MouseButtons buttons=Qt::NoButton;
    QTimer zoomTimer;
    QPoint globalPoint() const {return host->mapToGlobal(cursor.toPoint());}
    QWidget* target() const {
        QWidget* top=embeddedDialog && embeddedDialog->isVisible()
            ? static_cast<QWidget*>(embeddedDialog.data()) : QApplication::activePopupWidget();
        if(!top)top=QApplication::activeModalWidget();
        if(top) {
            if(!top->rect().contains(top->mapFromGlobal(globalPoint())))
                return modalShield && modalShield->isVisible() ? modalShield.data() : top;
            auto* child=top->childAt(top->mapFromGlobal(globalPoint()));
            return child?child:top;
        }
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
        mouseButton(Qt::MiddleButton,false);
        for(auto* key:mouseKeys)key->release();
        relative=false;twoFingerPan=false;suppressAfterPan=false;
        touchId=-1;zoomDeflection=0;zoomTicks=0;zoomTimer.stop();mouseReceiver.clear();
        if(zoomControl){QSignalBlocker blocker(zoomControl);zoomControl->setValue(0);}
    }
    bool isControl(QWidget* widget) const {
        for(auto* control:controls)if(widget==control || control->isAncestorOf(widget))return true;
        return false;
    }
    bool inRender(QWidget* widget) const {
        if(!cad || !widget || (embeddedDialog && embeddedDialog->isVisible()) ||
           QApplication::activeModalWidget() || QApplication::activePopupWidget())return false;
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
        QWidget* top=embeddedDialog && embeddedDialog->isVisible()
            ? static_cast<QWidget*>(embeddedDialog.data()) : QApplication::activePopupWidget();
        if(!top)top=QApplication::activeModalWidget();
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
    void begin(const QPointF& point) {
        relative=true;moved=false;start=last=point;clock.start();
        // Qt global touch coordinates and QMouseEvent positions are LOGICAL
        // pixels. Do not multiply by devicePixelRatio: GL/FBO alone does that.
        DrnasDiagnostics::note(QStringLiteral("TOUCH_VIEW_BEGIN global=%1,%2 host=%3x%4 dpr=%5")
            .arg(point.x()).arg(point.y()).arg(host->width()).arg(host->height())
            .arg(host->devicePixelRatioF()));
    }
    void move(const QPointF& point) {
        if(!relative || point==last)return;
        if((point-start).manhattanLength()>QApplication::startDragDistance())moved=true;
        cursor+=point-last;last=point;showPointer();sendMouse(QEvent::MouseMove,Qt::NoButton);
    }
    void end(const QPointF& point) {
        if(!relative)return;move(point);relative=false;
        if(!moved && clock.elapsed()<350 && buttons==Qt::NoButton) {
            mouseButton(Qt::LeftButton,true);mouseButton(Qt::LeftButton,false);
        }
        DrnasDiagnostics::note(QStringLiteral("TOUCH_VIEW_END moved=%1 cursor=%2,%3 held=%4")
            .arg(moved).arg(cursor.x()).arg(cursor.y()).arg(int(buttons)));
    }
    // Coin3D works in whole wheel notches (angleDelta 120). The old code
    // issued 60-unit half-notches every 16ms, flooding the event queue and
    // making touch zoom inconsistent with the desktop navigation handler.
    void zoomStep() {
        if(!cad || (embeddedDialog && embeddedDialog->isVisible()) ||
           QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
            zoomDeflection=0;zoomTimer.stop();return;
        }
        if(std::abs(zoomDeflection)<6){zoomTimer.stop();return;}
        QWidget* view=nullptr;
        if(auto* mdi=cad->findChild<QMdiArea>())
            if(auto* active=mdi->activeSubWindow()) {
                for(auto* candidate:active->findChildren<QGraphicsView*>())
                    if(candidate->isVisible()) {view=candidate->viewport();break;}
                if(!view)for(auto* candidate:active->findChildren<QOpenGLWidget*>())
                    if(candidate->isVisible()){view=candidate;break;}
            }
        if(!view || !view->isVisible()){zoomTimer.stop();return;}
        const int magnitude=std::abs(zoomDeflection);
        const int stride=magnitude<25?5:magnitude<55?3:magnitude<80?2:1;
        if(++zoomTicks%stride!=0)return;
        const QPointF local=view->rect().center();
        const QPointF global=view->mapToGlobal(local.toPoint());
        QCoreApplication::postEvent(view,new QWheelEvent(local,global,QPoint(),
            QPoint(0,zoomDeflection>0?120:-120),
            Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false));
        if(zoomTicks==stride || zoomTicks%32==0)
            DrnasDiagnostics::note(QStringLiteral("TOUCH_ZOOM notch=%1 view=%2x%3 dpr=%4")
                .arg(zoomDeflection>0?1:-1).arg(view->width()).arg(view->height())
                .arg(view->devicePixelRatioF()));
    }
    // An upstream desktop QDialog creates a second Android QWindow even when
    // parented to embedded FreeCAD. For the observed PartDesign Active Body
    // editor only, turn it into an in-window QWidget BEFORE native creation.
    // Preserve QDialog::accept/reject/exec semantics and its original owner.
    void layoutEmbeddedModal() {
        if(!embeddedDialog || !modalShield || !embeddedDialog->isVisible())return;
        modalShield->setGeometry(host->rect());
        modalShield->show();modalShield->raise();
        const QSize limit(std::max(1,host->width()-24),std::max(1,host->height()-24));
        embeddedDialog->setMinimumSize(0,0);
        QSize desired=embeddedDialog->sizeHint();
        if(!desired.isValid())desired=embeddedDialog->size();
        embeddedDialog->resize(desired.boundedTo(limit));
        embeddedDialog->move(std::max(0,(host->width()-embeddedDialog->width())/2),
                             std::max(0,(host->height()-embeddedDialog->height())/2));
        embeddedDialog->raise();
        if(auto* close=modalShield->findChild<QPushButton*>(QStringLiteral("drnasModalClose")))
            close->setGeometry(std::max(0,host->width()-118),4,114,32);
        DrnasDiagnostics::note(QStringLiteral("MODAL_EMBED layout=%1x%2 dialog=%3x%4 topLevel=%5")
            .arg(host->width()).arg(host->height())
            .arg(embeddedDialog->width()).arg(embeddedDialog->height())
            .arg(embeddedDialog->isWindow()));
    }
    void embedPartDesignModal(QDialog* dialog) {
        if(!dialog || dialog->property("drnasModalEmbedded").toBool() ||
           dialog->property("drnasModalRestoring").toBool() || !host)return;
        // Only the specific modal proven to fight QtActivity in device logs.
        if(!QString::fromLatin1(dialog->metaObject()->className()).contains(QStringLiteral("DlgActiveBody")))
            return;
        if(embeddedDialog && embeddedDialog!=dialog)return;
        originalDialogParent=dialog->parentWidget();
        originalDialogFlags=dialog->windowFlags();
        embeddedDialog=dialog;
        dialog->setProperty("drnasModalEmbedded",true);
        if(!modalShield) {
            modalShield=new QWidget(host);
            modalShield->setObjectName(QStringLiteral("drnasModalShield"));
            modalShield->setStyleSheet(QStringLiteral("background-color:rgba(10,18,30,175);"));
            auto* close=new QPushButton(QStringLiteral("× Zatvori"),modalShield);
            close->setObjectName(QStringLiteral("drnasModalClose"));
            close->setFocusPolicy(Qt::NoFocus);
            QObject::connect(close,&QPushButton::clicked,this,[this]{
                if(embeddedDialog)embeddedDialog->reject();
            });
            modalShield->hide();
        }
        // Qt::Widget rather than Qt::Dialog is essential: a dialog window
        // flag creates a separate native surface on this Qt/Android backend.
        dialog->setParent(host,Qt::Widget);
        DrnasDiagnostics::note(QStringLiteral("MODAL_EMBED prepared %1 ownedByHost=1")
            .arg(dialog->objectName()));
        QPointer<QDialog> guarded(dialog);
        QObject::connect(dialog,&QObject::destroyed,this,[this,guarded]{
            if(!guarded && modalShield)modalShield->hide();
            embeddedDialog.clear();
            for(auto* control:controls)control->setEnabled(true);
        });
    }
    void finishEmbeddedModal(QDialog* dialog) {
        if(!dialog || dialog!=embeddedDialog)return;
        if(modalShield)modalShield->hide();
        const QPointer<QDialog> guarded(dialog);
        const QPointer<QWidget> original=originalDialogParent;
        const Qt::WindowFlags flags=originalDialogFlags;
        embeddedDialog.clear();
        // Restore ownership only after the QDialog hide/exec path completes.
        QTimer::singleShot(0,this,[guarded,original,flags]{
            if(!guarded)return;
            guarded->setProperty("drnasModalRestoring",true);
            guarded->setProperty("drnasModalEmbedded",false);
            guarded->setParent(original.data(),flags);
            guarded->hide();
            guarded->setProperty("drnasModalRestoring",false);
        });
        DrnasDiagnostics::note(QStringLiteral("MODAL_EMBED finished; native focus conflict released"));
    }
    void fitDialog(QDialog* dialog) {
        if(!dialog || !dialog->isVisible() || !dialog->parentWidget() ||
           !(dialog->parentWidget()==host || host->isAncestorOf(dialog->parentWidget())))return;
        const QRect bounds(host->mapToGlobal(QPoint()),host->size());
        // A dialog already on screen must retain its own size, position and focus.
        if(bounds.contains(dialog->frameGeometry()))return;
        if(dialog->property("drnasLastDialogBounds").toRect()==bounds)return;
        dialog->setProperty("drnasLastDialogBounds",bounds);
        const QSize frame=dialog->frameGeometry().size()-dialog->size();
        const QSize usable=QSize(std::max(1,bounds.width()-std::max(0,frame.width())-12),
                                 std::max(1,bounds.height()-std::max(0,frame.height())-12));
        dialog->setMinimumSize(0,0);
        dialog->resize(dialog->size().boundedTo(usable));
        dialog->move(bounds.center()-QPoint(dialog->width()/2,dialog->height()/2));
        DrnasDiagnostics::note(QStringLiteral("MODAL_FIT dialog=%1 screen=%2x%3")
            .arg(dialog->objectName()).arg(bounds.width()).arg(bounds.height()));
    }
    void repair() {
        repairQueued=false;
        if(!cad)return;
        if(QWidget* modal=QApplication::activeModalWidget()) {
            fitDialog(qobject_cast<QDialog*>(modal));
            return; // Never maximize MDI documents or raise host controls during exec().
        }
        if(QApplication::activePopupWidget())return;
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
        for(auto* top:QApplication::topLevelWidgets())
            if(auto* dialog=qobject_cast<QDialog*>(top))fitDialog(dialog);
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
            if(embeddedDialog)layoutEmbeddedModal();
            DrnasDiagnostics::note(QStringLiteral("TOUCH_VIEW_RESIZE logical=%1x%2 dpr=%3")
                .arg(host->width()).arg(host->height()).arg(host->devicePixelRatioF()));
        }
        if((type==QEvent::Show || type==QEvent::Resize) &&
            (qobject_cast<QDialog*>(object)||qobject_cast<QMdiSubWindow*>(object)||
             (widget && widget->objectName().startsWith(QStringLiteral("Overlay")))))scheduleRepair();
        if(auto* dialog=qobject_cast<QDialog*>(object)) {
            // Polish is emitted before QWidget allocates an Android native
            // QWindow, unlike Show which may already be too late.
            if(type==QEvent::Polish)embedPartDesignModal(dialog);
            if(type==QEvent::Show && dialog==embeddedDialog) {
                QTimer::singleShot(0,this,[this]{layoutEmbeddedModal();});
            }
            if(type==QEvent::Show && (dialog->isModal() || dialog==embeddedDialog)) {
                releaseAll();mark->hide();
                for(auto* control:controls)control->setEnabled(false);
                DrnasDiagnostics::note(QStringLiteral("MODAL_BEGIN %1; host controls suspended").arg(dialog->objectName()));
            } else if(type==QEvent::Hide || type==QEvent::Destroy) {
                if(dialog==embeddedDialog)finishEmbeddedModal(dialog);
                QTimer::singleShot(0,this,[this] {
                    if(QApplication::activeModalWidget())return;
                    for(auto* control:controls)control->setEnabled(true);
                    scheduleRepair();
                });
            }
        }
        if(!widget || isControl(widget))return false;
        // Ordinary Qt controls, especially dialog buttons, keep direct touch.
        // Only the 3D surface behaves as a relative touchpad.
        if(!inRender(widget))return false;
        if(type==QEvent::TouchCancel){releaseAll();event->accept();return true;}
        if(type==QEvent::TouchBegin || type==QEvent::TouchUpdate || type==QEvent::TouchEnd) {
            // One finger: move the logical pointer, tap: select.
            // Two fingers: synthetic middle-button drag for native FreeCAD
            // CAD-navigation pan. No additional floating UI is needed.
            const auto points=static_cast<QTouchEvent*>(event)->points();
            QPointF sum;
            int activeCount=0;
            for(const auto& point:points)
                if(point.state()!=QEventPoint::State::Released){
                    sum+=point.globalPosition();++activeCount;
                }
            if(activeCount>=2) {
                const QPointF middle=sum/double(activeCount);
                if(!twoFingerPan){
                    relative=false;moved=true;touchId=-1;
                    cursor=host->mapFromGlobal(middle.toPoint());
                    showPointer();
                    mouseButton(Qt::MiddleButton,true);
                    twoFingerPan=true;panLast=middle;suppressAfterPan=true;
                    DrnasDiagnostics::note(QStringLiteral("TOUCH_PAN_BEGIN two-finger middle-drag dpr=%1")
                        .arg(host->devicePixelRatioF()));
                } else if(middle!=panLast) {
                    cursor+=middle-panLast;panLast=middle;
                    showPointer();sendMouse(QEvent::MouseMove,Qt::NoButton);
                }
                event->accept();return true;
            }
            if(twoFingerPan){
                mouseButton(Qt::MiddleButton,false);
                twoFingerPan=false;relative=false;touchId=-1;
                DrnasDiagnostics::note(QStringLiteral("TOUCH_PAN_END"));
            }
            // A finger left from a two-finger pan must NOT become a click.
            if(suppressAfterPan){
                if(activeCount==0)suppressAfterPan=false;
                event->accept();return true;
            }
            for(const auto& point:points) {
                if(touchId<0 && point.state()==QEventPoint::State::Pressed){touchId=point.id();begin(point.globalPosition());}
                if(point.id()!=touchId)continue;
                if(point.state()==QEventPoint::State::Released){end(point.globalPosition());touchId=-1;}
                else move(point.globalPosition());
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
        connect(zoom,&QSlider::valueChanged,this,[this](int value) {
            const int next=std::abs(value)<6?0:value;
            if(zoomDeflection==next)return;
            zoomDeflection=next;zoomTicks=0;
            if(zoomDeflection)zoomTimer.start();
            else zoomTimer.stop();
        });
        connect(zoom,&QSlider::sliderReleased,this,[this,zoom] {
            QSignalBlocker block(zoom);zoom->setValue(0);
            zoomDeflection=0;zoomTicks=0;zoomTimer.stop();
        });
        zoomTimer.setInterval(65);connect(&zoomTimer,&QTimer::timeout,this,[this]{zoomStep();});
        qApp->installEventFilter(this);scheduleRepair();showPointer();
        DrnasDiagnostics::note(QStringLiteral("INPUT minimal: movable L/R + zoom; direct dialogs; relative CAD viewport"));
    }
};

