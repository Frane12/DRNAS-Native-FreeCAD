// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QApplication>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QWidget>
#include <QWindow>
#include "StartupDiagnostics.h"

// Native Qt/Android owns the EGLSurface, QRhi backing store and OpenGL widget
// composition. In device reports the former v2 guard repeatedly disabled ALL
// QWidget updates on Expose/Show/Resize and swallowed Paint/UpdateRequest,
// including during menus and rotation. That can starve QRhi of its current
// surface and makes an otherwise valid Coin3D FBO look like an empty canvas.
//
// This guard is deliberately OBSERVATIONAL. It never pauses QWidget updates,
// forces a makeCurrent, consumes Qt events, or asks the window to recompose.
// A native Qt surface failure will now remain visible in diagnostics instead
// of being amplified by synthetic pause/resume cycles.
class DrnasRenderSurfaceGuard final : public QObject {
    QPointer<QWidget> host;
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if (!host || !event) return false;
        if (object==host->windowHandle() &&
            event->type()==QEvent::PlatformSurface) {
            const auto kind=static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
            DrnasDiagnostics::note(kind==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed
                ? QStringLiteral("SURFACE v3: Android surface about to be destroyed (Qt owns lifecycle)")
                : QStringLiteral("SURFACE v3: Android surface created (Qt owns lifecycle)"));
        }
        if (object==host && event->type()==QEvent::Resize) {
            DrnasDiagnostics::note(QStringLiteral("SURFACE v3: host resize %1x%2 dpr=%3; no forced GL refresh")
                .arg(host->width()).arg(host->height()).arg(host->devicePixelRatioF()));
        }
        return false;
    }
public:
    explicit DrnasRenderSurfaceGuard(QWidget* window):QObject(window),host(window) {
        qApp->installEventFilter(this);
        DrnasDiagnostics::note(QStringLiteral(
            "SURFACE v3: passive Qt/Android lifecycle observer; QRhi/GL composition untouched"));
    }
};
