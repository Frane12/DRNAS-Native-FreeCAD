// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QMainWindow>
#include <QDockWidget>
#include <QToolBar>
#include <QTimer>
#include <QEvent>
#include <QStatusBar>
#include <QPointer>

// Adapt dock placement in Qt logical coordinates; never resize a native surface.
class DrnasMobileLayout final : public QObject {
    QPointer<QMainWindow> host, cad;
    QPointer<QToolBar> commands;
    QTimer settle;
    int orientation=-1;
    bool portraitPanels=false, landscapePanels=true, applying=false;
    QList<QDockWidget*> modelDocks() const {
        QList<QDockWidget*> docks;
        if (!cad) return docks;
        for (auto* dock:cad->findChildren<QDockWidget*>()) {
            const QString name=dock->objectName().toLower();
            if (name.contains(QStringLiteral("tree")) || name.contains(QStringLiteral("property")) ||
                name.contains(QStringLiteral("combo")) || name.contains(QStringLiteral("task")))
                docks.append(dock);
        }
        return docks;
    }
    void arrange(bool updateVisibility=false) {
        if (!host || !cad || applying) return;
        applying=true;
        const bool portrait=host->height()>host->width();
        const bool changed=orientation!=int(portrait);
        orientation=int(portrait);
        const auto docks=modelDocks();
        const auto area=portrait ? Qt::BottomDockWidgetArea : Qt::LeftDockWidgetArea;
        const bool visible=portrait ? portraitPanels : landscapePanels;
        QDockWidget* first=nullptr;
        for (auto* dock:docks) {
            dock->setMinimumSize(0,0);
            dock->setFeatures(QDockWidget::DockWidgetClosable);
            if (changed || cad->dockWidgetArea(dock)!=area || dock->isFloating()) {
                dock->setFloating(false);
                cad->addDockWidget(area,dock);
                if (first) cad->tabifyDockWidget(first,dock);
            }
            if (!first) first=dock;
            if (changed || updateVisibility) dock->setVisible(visible);
        }
        if (first && visible) {
            const int extent=portrait ? qMax(1,cad->height()/4) : qMax(1,qMin(250,cad->width()/4));
            cad->resizeDocks(docks,QList<int>(docks.size(),extent),portrait?Qt::Vertical:Qt::Horizontal);
            if (changed) first->raise();
        }
        // Full desktop toolbar rows can consume most of a portrait viewport.
        // Genuine commands stay reachable through the live Menu and Rad menus.
        for (auto* bar:cad->findChildren<QToolBar*>()) {
            if (bar==commands) continue;
            bar->setMinimumSize(0,0);
            bar->setIconSize(QSize(18,18));
            if (portrait) {
                if (!bar->property("drnasPortraitHidden").toBool())
                    bar->setProperty("drnasLandscapeVisible",bar->isVisible());
                bar->setProperty("drnasPortraitHidden",true);
                bar->hide();
            } else if (bar->property("drnasPortraitHidden").toBool()) {
                bar->setProperty("drnasPortraitHidden",false);
                bar->setVisible(bar->property("drnasLandscapeVisible").toBool());
            }
        }
        cad->statusBar()->hide();
        applying=false;
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if (!applying && (object==host || object==cad) && event->type()==QEvent::Resize)
            settle.start();
        return false;
    }
public:
    DrnasMobileLayout(QMainWindow* shell,QMainWindow* workspace,QToolBar* bar)
        :QObject(shell),host(shell),cad(workspace),commands(bar) {
        auto* toggle=bar->addAction(QStringLiteral("Paneli"));
        connect(toggle,&QAction::triggered,this,[this]{
            if (!host) return;
            bool& visible=host->height()>host->width()?portraitPanels:landscapePanels;
            visible=!visible; arrange(true);
        });
        settle.setSingleShot(true); settle.setInterval(100);
        connect(&settle,&QTimer::timeout,this,[this]{arrange();});
        host->installEventFilter(this); cad->installEventFilter(this);
        QTimer::singleShot(350,this,[this]{arrange();});
        QTimer::singleShot(1800,this,[this]{arrange();});
    }
};
