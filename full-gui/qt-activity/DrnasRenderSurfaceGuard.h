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
    QList<QPointer<QOpenGLWidget>> suspended;
    bool surfaceLost=false, paused=false, queued=false;

    bool ready() const {
        QWindow* window=host ? host->windowHandle() : nullptr;
        return window && !surfaceLost && host->isVisible() &&
            window->isExposed() && window->width()>0 && window->height()>0 &&
            QGuiApplication::applicationState()==Qt::ApplicationActive;
    }
    void pause() {
        if (!host) return;
        if (!paused) {
            paused=true;
            DrnasDiagnostics::note(QStringLiteral("SURFACE: pause GL widget updates; native surface unavailable"));
        }
        for (auto* widget:host->findChildren<QOpenGLWidget*>()) {
            if (!widget->updatesEnabled()) continue;
            // Only restore update flags that this guard changed.
            suspended.append(QPointer<QOpenGLWidget>(widget));
            widget->setUpdatesEnabled(false);
        }
    }
    void refresh() {
        queued=false;
        if (!host) return;
        QWindow* window=host->windowHandle();
        if (observedWindow!=window) {
            observedWindow=window;
            surfaceLost=false; // A new handle must still pass isExposed().
        }
        if (!ready()) { pause(); return; }
        const bool wasPaused=paused;
        paused=false;
        const auto restore=suspended;
        suspended.clear();
        for (const auto& widget:restore) if (widget) widget->setUpdatesEnabled(true);
        if (wasPaused) {
            DrnasDiagnostics::note(QStringLiteral("SURFACE: exposed again; resume Qt-managed GL updates"));
            // Queue one normal repaint. Qt owns context/FBO initialization;
            // do not spin on an invalid context or force an EGL retry loop.
            for (auto* widget:host->findChildren<QOpenGLWidget*>())
                if (widget->isVisible() && widget->updatesEnabled()) widget->update();
            host->update();
        }
    }
    void scheduleRefresh() {
        if (queued) return;
        queued=true;
        QTimer::singleShot(0,this,[this]{refresh();});
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
                if (surfaceLost) pause();
                scheduleRefresh();
            } else if (event->type()==QEvent::Expose) {
                if (!window->isExposed()) pause();
                scheduleRefresh(); // Let Qt finish the expose transition first.
            }
        }
        if (event->type()==QEvent::ApplicationStateChange) {
            if (QGuiApplication::applicationState()!=Qt::ApplicationActive) pause();
            scheduleRefresh();
        }
        if (object==host && (event->type()==QEvent::Show || event->type()==QEvent::Hide ||
                            event->type()==QEvent::WinIdChange)) scheduleRefresh();
        auto* widget=qobject_cast<QOpenGLWidget*>(object);
        if (widget && host->isAncestorOf(widget)) {
            if (event->type()==QEvent::Show || event->type()==QEvent::ParentChange) scheduleRefresh();
            if ((event->type()==QEvent::Paint || event->type()==QEvent::UpdateRequest) && !ready()) {
                scheduleRefresh();
                return true;
            }
        }
        // Do not let a queued composition target the shell's destroyed
        // surface. Expose/surface events themselves always reach Qt.
        if (object==host && event->type()==QEvent::UpdateRequest && !ready()) {
            scheduleRefresh();
            return true;
        }
        return false;
    }
public:
    explicit DrnasRenderSurfaceGuard(QWidget* window):QObject(window),host(window) {
        qApp->installEventFilter(this);
        scheduleRefresh();
        DrnasDiagnostics::note(QStringLiteral("SURFACE: lifecycle guard installed on existing QtActivity window"));
    }
};
