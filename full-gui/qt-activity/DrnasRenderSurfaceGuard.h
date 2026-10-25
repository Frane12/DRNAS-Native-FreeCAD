// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QApplication>
#include <QOpenGLWidget>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include "StartupDiagnostics.h"

// Observe QtActivity's existing surface. Never create a context, call winId(),
// makeCurrent(), reparent a GL widget, or rebuild the native window here.
class DrnasRenderSurfaceGuard final : public QObject {
    QPointer<QWidget> host;
    QPointer<QWindow> observedWindow;
    QTimer resumeTimer;
    bool surfaceLost=false, paused=false, restoreHostUpdates=false;
    bool needsRepaint=true, shuttingDown=false;

    bool ready() const {
        QWindow* window=host ? host->windowHandle() : nullptr;
        return !shuttingDown && window && !surfaceLost && host->isVisible() &&
            window->isExposed() && window->width()>0 && window->height()>0 &&
            QGuiApplication::applicationState()==Qt::ApplicationActive;
    }
    void pause() {
        if (!host) return;
        resumeTimer.stop();
        needsRepaint=true;
        if (!paused) {
            paused=true;
            // QGraphicsView can call QuarterWidget::paintEvent without a
            // QOpenGLWidget paint event. Suspend the whole backing store,
            // including Qt's QRhi composition, rather than only GL children.
            restoreHostUpdates=host->updatesEnabled();
            if (restoreHostUpdates) host->setUpdatesEnabled(false);
            DrnasDiagnostics::note(QStringLiteral("SURFACE v2: pause whole-window updates and composition"));
        }
    }
    void refresh() {
        if (!host) return;
        QWindow* window=host->windowHandle();
        if (observedWindow!=window) {
            observedWindow=window;
            surfaceLost=false; // A new handle must still pass isExposed().
        }
        if (!ready()) { pause(); return; }
        const bool wasPaused=paused;
        paused=false;
        // QWidget restores inherited update flags, preserving children that
        // were explicitly disabled before this guard suspended the parent.
        if (restoreHostUpdates) host->setUpdatesEnabled(true);
        restoreHostUpdates=false;
        if (wasPaused || needsRepaint) {
            needsRepaint=false;
            DrnasDiagnostics::note(QStringLiteral("SURFACE v2: resume %1x%2 after settled expose/resize")
                .arg(host->width()).arg(host->height()));
            // Queue one normal repaint. Qt owns context/FBO initialization;
            // do not spin on an invalid context or force an EGL retry loop.
            for (auto* widget:host->findChildren<QOpenGLWidget*>())
                if (widget->isVisible() && widget->updatesEnabled()) widget->update();
            host->update();
            window->requestUpdate(); // Replace any composition request dropped while paused.
        }
    }
    void scheduleRefresh() {
        if (shuttingDown) return;
        // Restart on lifecycle events so a resize/expose burst must settle
        // before Qt paints. Paint/update events never restart this timer.
        resumeTimer.start();
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if (!host) return false;
        QWindow* window=host->windowHandle();
        if (object==window && window) {
            if (observedWindow!=window) { observedWindow=window; surfaceLost=false; }
            if (event->type()==QEvent::PlatformSurface) {
                const auto kind=static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
                surfaceLost=kind==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed;
                DrnasDiagnostics::note(surfaceLost
                    ? QStringLiteral("SURFACE: native surface about to be destroyed")
                    : QStringLiteral("SURFACE: native surface created; waiting for expose"));
                pause();
                scheduleRefresh();
            } else if (event->type()==QEvent::Expose) {
                pause();
                scheduleRefresh(); // Let Qt finish the expose transition first.
            } else if (event->type()==QEvent::Resize) {
                pause();
                scheduleRefresh();
            }
        }
        if (event->type()==QEvent::ApplicationStateChange) {
            pause();
            scheduleRefresh();
        }
        if (object==host && (event->type()==QEvent::Show || event->type()==QEvent::Hide ||
                            event->type()==QEvent::WinIdChange)) {
            pause();
            scheduleRefresh();
        }
        if (object==host && event->type()==QEvent::Resize) {
            // Rotation can resize an existing surface without destroying it.
            // Coalesce resize bursts, then repaint at the new Qt dimensions.
            // Keep this pending if EGL surface exposure has not returned yet.
            pause();
            scheduleRefresh();
        }
        auto* widget=qobject_cast<QWidget*>(object);
        if (widget && host->isAncestorOf(widget)) {
            if (qobject_cast<QOpenGLWidget*>(widget) &&
                (event->type()==QEvent::Show || event->type()==QEvent::ParentChange)) {
                pause();
                scheduleRefresh();
            }
        }
        // Do not let a queued composition target the shell's destroyed
        // surface. Expose/surface events themselves always reach Qt.
        const bool paintTarget=object==window || object==host ||
            (widget && host->isAncestorOf(widget));
        if (paintTarget && (event->type()==QEvent::UpdateRequest || event->type()==QEvent::Paint) &&
            (paused || !ready())) {
            // Do not turn a pending update into an EGL retry loop. Resume
            // only from surface/expose/resize/application lifecycle events.
            if (!paused) pause();
            return true;
        }
        return false;
    }
public:
    explicit DrnasRenderSurfaceGuard(QWidget* window):QObject(window),host(window) {
        resumeTimer.setSingleShot(true);
        resumeTimer.setInterval(64);
        QObject::connect(&resumeTimer,&QTimer::timeout,this,[this]{refresh();});
        QObject::connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{
            shuttingDown=true;
            pause();
            DrnasDiagnostics::note(QStringLiteral("SURFACE v2: shutdown; rendering remains suspended"));
        });
        qApp->installEventFilter(this);
        pause();
        scheduleRefresh();
        DrnasDiagnostics::note(QStringLiteral("SURFACE v2: whole-window lifecycle guard installed on existing QtActivity window"));
    }
};
