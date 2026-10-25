// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <QMainWindow>
#include <QPalette>
#include <QPointer>
#include <QResizeEvent>
#include <QWidget>
#include "StartupDiagnostics.h"

// Center a genuine 1024x768 FreeCAD MainWindow on Android's wide landscape
// surface. Native Qt paints and hit-tests one logical coordinate system;
// never resample a Coin3D framebuffer or independently scale mouse events.
class DrnasDesktopWorkspace final : public QWidget {
    QPointer<QMainWindow> cad;
    static constexpr int pcWidth=1024, pcHeight=768;
    void fit() {
        if (!cad || width()<1 || height()<1) return;
        const double fitScale=std::clamp(std::min(
            double(width())/pcWidth, double(height())/pcHeight),0.001,1.0);
        const QSize target(std::max(1,int(std::floor(pcWidth*fitScale))),
                           std::max(1,int(std::floor(pcHeight*fitScale))));
        // Do not setFixedSize(): that propagates 768px min-height into the
        // native QtActivity window and blocks rotation/keyboard resizing.
        const QRect wanted(QPoint((width()-target.width())/2,
                                  (height()-target.height())/2), target);
        if (cad->geometry()!=wanted) {
            cad->setGeometry(wanted);
            DrnasDiagnostics::note(QStringLiteral(
                "PC DESKTOP layout: surface=%1x%2 CAD=%3x%4 inset=%5x%6 dpr=%7")
                .arg(width()).arg(height()).arg(target.width()).arg(target.height())
                .arg((width()-target.width())/2).arg((height()-target.height())/2)
                .arg(devicePixelRatioF()));
        }
    }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        fit();
    }
public:
    explicit DrnasDesktopWorkspace(QWidget* parent=nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("drnasPcDesktopWorkspace"));
        setMinimumSize(0,0);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        setAutoFillBackground(true);
        QPalette bg=palette();
        bg.setColor(QPalette::Window,QColor(19,29,42));
        setPalette(bg);
    }
    void attach(QMainWindow* window) {
        cad=window;
        cad->setMinimumSize(0,0);
        cad->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);
        cad->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        // This direct child has no parent layout minimum-size contribution.
        // Every widget retains native Qt hit testing at its real location.
        fit();
    }
};
