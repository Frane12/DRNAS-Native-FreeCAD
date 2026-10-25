// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QApplication>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QWidget>
#include <QWindow>
#include "StartupDiagnostics.h"

// Qt owns surface lifetime, exposure, resize, FBO recreation and composition.
// Toolbar/dock layout changes are normal QWidget operations, not surface loss.
// This observer never disables updates, consumes events or touches a context.
class DrnasRenderSurfaceGuard final : public QObject {
    QPointer<QWidget> host;
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (host && object==host->windowHandle() &&
            event->type()==QEvent::PlatformSurface) {
            const auto kind=static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
            DrnasDiagnostics::note(kind==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed
                ? QStringLiteral("SURFACE v3: Qt surface destruction observed")
                : QStringLiteral("SURFACE v3: Qt surface creation observed"));
        }
        return false;
    }
public:
    explicit DrnasRenderSurfaceGuard(QWidget* window):QObject(window),host(window) {
        qApp->installEventFilter(this);
        DrnasDiagnostics::note(QStringLiteral("SURFACE v3: passive observer; Qt owns all painting and resize"));
    }
};
