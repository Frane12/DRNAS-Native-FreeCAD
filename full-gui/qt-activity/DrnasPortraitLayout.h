// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QLayout>
#include <QPointer>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <algorithm>
#include <functional>
#include <utility>
#include "StartupDiagnostics.h"

// Change the desktop dock layout, never the document's GL widget or QWindow.
class DrnasPortraitLayout final : public QObject {
    QPointer<QMainWindow> host, cad;
    QTimer settle;
    bool arranging=false;
    QSize previousSize;
    std::function<void()> disableOverlays;
    void arrange() {
        if (!host || !cad || arranging || host->width()>=host->height()) return;
        arranging=true;
        // A saved empty overlay list doesn't remove already restored panels.
        // ToggleAll means DisableAll only while a real overlay contains docks.
        bool overlaid=false;
        for (auto* tabs:cad->findChildren<QTabWidget*>())
            if (tabs->inherits("Gui::OverlayTabWidget") && tabs->count()>0)
                overlaid=true;
        if (overlaid && disableOverlays) disableOverlays();

        // Desktop minimum hints must never resize QtActivity past its surface.
        // Scrollers below keep wide task controls reachable inside that width.
        if (host->layout()) host->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        if (cad->layout()) cad->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        host->setMinimumSize(0,0);
        cad->setMinimumSize(0,0);
        cad->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        QList<QDockWidget*> panels;
        for (auto* dock:cad->findChildren<QDockWidget*>()) {
            // Current upstream calls the combined tree/property dock "Model".
            // Include optional panels too, when the user opens them via View.
            dock->setFeatures(dock->features() & ~(QDockWidget::DockWidgetFloatable |
                                                   QDockWidget::DockWidgetMovable));
            dock->setAllowedAreas(Qt::BottomDockWidgetArea);
            // Upstream installs OverlayTitleBar on ordinary docks as well.
            // Replace that drag/overlay UI; bottom tabs already show the title.
            auto* oldTitle=dock->titleBarWidget();
            if (!oldTitle || oldTitle->objectName()!=QStringLiteral("drnasPhoneDockTitle")) {
                auto* title=new QWidget(dock);
                title->setObjectName(QStringLiteral("drnasPhoneDockTitle"));
                title->setFixedHeight(0);
                dock->setTitleBarWidget(title);
                if (oldTitle) oldTitle->deleteLater();
            }
            if (auto* content=dock->widget()) {
                // DockWindowManager returns dock->widget() as the original
                // TaskView/ComboView type. Never wrap or replace that object.
                content->setMinimumWidth(0);
                auto policy=content->sizePolicy();
                policy.setHorizontalPolicy(QSizePolicy::Ignored);
                content->setSizePolicy(policy);
                for (auto* scroll:content->findChildren<QScrollArea*>()) {
                    scroll->setMinimumWidth(0);
                    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                }
            }
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
                        dock->objectName()==QStringLiteral("Combo View") ||
                        dock->objectName()==QStringLiteral("Model")) {dock->raise();break;}
            }
            // Size only on first placement and actual screen/keyboard resize.
            // User splitter adjustments and active Tasks tabs stay in place.
            if (previousSize!=host->size() || !cad->property("drnasPortraitPlaced").toBool()) {
                cad->resizeDocks(panels,QList<int>(panels.size(),std::min(240,host->height()/4)),Qt::Vertical);
                previousSize=host->size();
                cad->setProperty("drnasPortraitPlaced",true);
                DrnasDiagnostics::note(QStringLiteral("PORTRAIT v2: bottom panel tabs=%1 host=%2x%3; desktop drag titles removed")
                    .arg(panels.size()).arg(host->width()).arg(host->height()));
            }
        }
        for (auto* bar:cad->findChildren<QToolBar*>()) {
            bar->setMinimumWidth(0);
            bar->setIconSize(QSize(18,18));
        }
        // Several desktop toolbars can share a row whose combined minimum
        // is wider than the phone, even though each bar has overflow buttons.
        int rowWidth=0;
        for (auto* bar:cad->findChildren<QToolBar*>(QString(),Qt::FindDirectChildrenOnly)) {
            if (!bar->isVisible() || cad->toolBarArea(bar)!=Qt::TopToolBarArea) continue;
            if (cad->toolBarBreak(bar)) rowWidth=0;
            const int minimum=std::max(0,bar->minimumSizeHint().width());
            if (rowWidth && rowWidth+minimum>host->width()) {
                cad->insertToolBarBreak(bar);
                rowWidth=0;
            }
            rowWidth+=minimum;
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
            (object==cad && type==QEvent::LayoutRequest) ||
            (type==QEvent::Show && (object==cad || qobject_cast<QDockWidget*>(object) ||
                                   qobject_cast<QToolBar*>(object)))) {
            if (!settle.isActive()) settle.start();
        }
        return false;
    }
public:
    DrnasPortraitLayout(QMainWindow* shell,QMainWindow* freecad,
                        std::function<void()> disable={})
        :QObject(shell),host(shell),cad(freecad),disableOverlays(std::move(disable)) {
        // Install before startup restores wide docks/toolbars, not after the
        // first delayed pass has already let their minimum hints grow the host.
        if (host->layout()) host->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        if (cad->layout()) cad->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        host->setMinimumSize(0,0);cad->setMinimumSize(0,0);
        cad->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        settle.setSingleShot(true);settle.setInterval(80);
        connect(&settle,&QTimer::timeout,this,[this]{arrange();});
        qApp->installEventFilter(this);
        settle.start();
    }
};
