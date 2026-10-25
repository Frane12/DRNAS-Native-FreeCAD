// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <QHBoxLayout>
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
        if (cad->size()!=target || cad->minimumSize()!=target || cad->maximumSize()!=target) {
            cad->setFixedSize(target);
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
        auto* row=new QHBoxLayout(this);
        row->setContentsMargins(0,0,0,0);
        row->setSpacing(0);
        row->setSizeConstraint(QLayout::SetNoConstraint);
    }
    void attach(QMainWindow* window) {
        cad=window;
        cad->setMinimumSize(0,0);
        cad->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        layout()->addWidget(cad);
        static_cast<QHBoxLayout*>(layout())->setAlignment(cad,Qt::AlignCenter);
        fit();
    }
};
