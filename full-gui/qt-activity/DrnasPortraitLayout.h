// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QPointer>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <algorithm>
#include "StartupDiagnostics.h"

// Change the desktop dock layout, never the document's GL widget or QWindow.
class DrnasPortraitLayout final : public QObject {
    QPointer<QMainWindow> host, cad;
    QTimer settle;
    bool arranging=false;
    QSize previousSize;
    void arrange() {
        if (!host || !cad || arranging || host->width()>=host->height()) return;
        arranging=true;
        QList<QDockWidget*> panels;
        for (auto* dock:cad->findChildren<QDockWidget*>()) {
            const QString name=dock->objectName();
            if (name!=QStringLiteral("Tree view") && name!=QStringLiteral("Property view") &&
                name!=QStringLiteral("Combo View") && name!=QStringLiteral("Tasks")) continue;
            dock->setFeatures(dock->features() & ~QDockWidget::DockWidgetFloatable);
            dock->setMinimumSize(0,0);
            dock->setMaximumWidth(QWIDGETSIZE_MAX);
            dock->setMaximumHeight(std::max(120,host->height()/3));
            if (dock->isFloating()) dock->setFloating(false);
            if (cad->dockWidgetArea(dock)!=Qt::BottomDockWidgetArea)
                cad->addDockWidget(Qt::BottomDockWidgetArea,dock);
            panels.append(dock);
        }
        if (!panels.isEmpty()) {
            auto* first=panels.first();
            for (auto* dock:panels) if (dock!=first && !cad->tabifiedDockWidgets(first).contains(dock))
                cad->tabifyDockWidget(first,dock);
            if (!cad->property("drnasPortraitPlaced").toBool()) {
                for (auto* dock:panels)
                    if (dock->objectName()==QStringLiteral("Tree view") ||
                        dock->objectName()==QStringLiteral("Combo View")) {dock->raise();break;}
            }
            // Size only on first placement and actual screen/keyboard resize.
            // User splitter adjustments and active Tasks tabs stay in place.
            if (previousSize!=host->size() || !cad->property("drnasPortraitPlaced").toBool()) {
                cad->resizeDocks(panels,QList<int>(panels.size(),std::min(240,host->height()/4)),Qt::Vertical);
                previousSize=host->size();
                cad->setProperty("drnasPortraitPlaced",true);
                DrnasDiagnostics::note(QStringLiteral("PORTRAIT: bottom panel tabs=%1 host=%2x%3")
                    .arg(panels.size()).arg(host->width()).arg(host->height()));
            }
        }
        for (auto* bar:cad->findChildren<QToolBar*>()) {
            bar->setMinimumWidth(0);
            bar->setIconSize(QSize(18,18));
        }
        for (auto* tabs:cad->findChildren<QTabBar*>()) {
            tabs->setUsesScrollButtons(true);
            tabs->setExpanding(false);
        }
        arranging=false;
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if (arranging || !cad) return false;
        const auto type=event->type();
        if ((object==host && type==QEvent::Resize) ||
            (type==QEvent::Show && (object==cad || qobject_cast<QDockWidget*>(object) ||
                                   qobject_cast<QToolBar*>(object)))) {
            if (!settle.isActive()) settle.start();
        }
        return false;
    }
public:
    DrnasPortraitLayout(QMainWindow* shell,QMainWindow* freecad)
        :QObject(shell),host(shell),cad(freecad) {
        settle.setSingleShot(true);settle.setInterval(80);
        connect(&settle,&QTimer::timeout,this,[this]{arrange();});
        qApp->installEventFilter(this);
        settle.start();
    }
};
