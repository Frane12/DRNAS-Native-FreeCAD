// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include "StartupDiagnostics.h"

// Android presents the upstream actions; FreeCAD still owns their command IDs,
// icons, handlers, state, menus, docks, document and Coin viewport.
class DrnasAndroidCommandUi final : public QWidget {
    QPointer<QMainWindow> host,cad;
    QComboBox* groups;
    QWidget *quickContent,*toolsContent;
    QHBoxLayout *quickLayout,*toolsLayout;
    QTimer refreshTimer;
    struct Group {
        QString key,title;
        QList<QPointer<QAction>> actions;
    };
    QList<Group> catalog;
    QStringList signature;
    QSet<const QMenu*> visitedMenus;
    bool refreshing=false;

    static QString caption(QString text) {
        text.replace(QStringLiteral("&&"),QString(QChar(0x1f)));
        text.remove('&');text.replace(QChar(0x1f),'&');
        return text;
    }
    static bool usable(QAction* action) {
        return action && !action->isSeparator() && !qobject_cast<QWidgetAction*>(action);
    }
    QToolButton* originalButton(QAction* action) const {
        if (!cad) return nullptr;
        for (auto* button:cad->findChildren<QToolButton*>())
            if (button->defaultAction()==action) return button;
        return nullptr;
    }
    QToolButton* mirror(QAction* action,QWidget* owner,bool compact) {
        auto* button=new QToolButton(owner);
        button->setDefaultAction(action); // SAME QAction: no copied command/state/shortcut.
        button->setObjectName(QStringLiteral("drnasAndroidAction_")+action->objectName());
        button->setProperty("drnasFreeCADCommandId",action->objectName());
        button->setFocusPolicy(Qt::NoFocus);
        button->setAttribute(Qt::WA_AcceptTouchEvents);
        button->setIconSize(QSize(28,28));
        button->setToolButtonStyle(compact && !action->icon().isNull()
            ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
        button->setFixedSize(compact ? QSize(56,52) : QSize(92,72));
        button->setAutoRepeat(false);
        // FreeCAD ActionGroup::addTo() puts some menus on the QToolButton,
        // not on QAction. Undo/Redo also have special toolbar actions/menus.
        if (auto* source=originalButton(action);source && source->menu()) {
            button->setMenu(source->menu());button->setPopupMode(source->popupMode());
        } else if (action->menu()) {
            button->setMenu(action->menu());button->setPopupMode(QToolButton::InstantPopup);
        }
        button->setVisible(action->isVisible());
        connect(action,&QAction::changed,button,[button,action] {
            button->setVisible(action->isVisible());
        });
        connect(action,&QObject::destroyed,button,&QWidget::hide);
        return button;
    }
    static void clearRow(QHBoxLayout* layout) {
        while (auto* item=layout->takeAt(0)) {
            if (auto* widget=item->widget()) {widget->hide();widget->deleteLater();}
            delete item;
        }
    }
    QScrollArea* scroller(QWidget* content,int height) {
        auto* scroll=new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("drnasAndroidCommandScroller"));
        scroll->setProperty("drnasCommandScroller",true);
        scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFixedHeight(height);scroll->setMinimumWidth(0);
        scroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
        scroll->setWidget(content);
        return scroll;
    }
    void collectMenu(QMenu* menu,const QString& path,QList<Group>& found) {
        if (!menu || visitedMenus.contains(menu)) return;
        visitedMenus.insert(menu);
        Group group{QStringLiteral("menu:%1").arg(quintptr(menu),0,16),path,{}};
        for (auto* action:menu->actions()) if (usable(action)) group.actions.append(action);
        if (!group.actions.isEmpty()) found.append(group);
        for (auto* action:menu->actions())
            if (action->menu()) collectMenu(action->menu(),path+QStringLiteral(" / ")+caption(action->text()),found);
    }
    void showGroup() {
        clearRow(toolsLayout);
        const QString key=groups->currentData().toString();
        for (const auto& group:catalog) if (group.key==key) {
            for (const auto& action:group.actions)
                if (action) toolsLayout->addWidget(mirror(action,toolsContent,false));
            break;
        }
        toolsLayout->addStretch();
        if (auto* scroll=qobject_cast<QScrollArea*>(toolsContent->parentWidget()->parentWidget()))
            scroll->horizontalScrollBar()->setValue(0);
    }
    void openMenu(QToolButton* button) {
        // Keep live upstream menus and their aboutToShow/aboutToHide hooks.
        auto* menu=button->menu();menu->clear();
        if (cad && cad->menuBar()) for (auto* action:cad->menuBar()->actions())
            if (action->menu()) menu->addMenu(action->menu());
        menu->addSeparator();
        const QList<QPair<QString,QString>> helpers={
            {QStringLiteral("Napredno"),QStringLiteral("drnasFloatingCommandsLauncher")},
            {QStringLiteral("Dijagnostika"),QStringLiteral("drnasDebugLauncher")},
            {QStringLiteral("LAB"),QStringLiteral("drnasLiveLabButton")}};
        for (const auto& helper:helpers) {
            auto* source=host ? host->findChild<QPushButton*>(helper.second) : nullptr;
            if (!source) continue;
            auto* action=menu->addAction(helper.first);
            connect(action,&QAction::triggered,source,&QAbstractButton::click);
        }
    }
    void refresh() {
        if (!cad || !host || refreshing) return;
        // Do not replace the widget under an active finger, dialog or popup.
        if (host->property("drnasUiTouchActive").toBool() ||
            QApplication::activePopupWidget() || host->property("drnasEmbeddedModalActive").toBool()) {
            refreshTimer.start();return;
        }
        refreshing=true;
        if (cad->menuBar()) cad->menuBar()->hide();
        QList<Group> found;
        for (auto* bar:cad->findChildren<QToolBar*>()) {
            // Collapse chrome without hiding it. FreeCAD's own visibility and
            // workbench switching remain observable; hidden bars stay hidden.
            bar->setMinimumSize(0,0);bar->setMaximumSize(0,0);
            bar->setAttribute(Qt::WA_TransparentForMouseEvents);
            if (bar->isHidden()) continue;
            Group group{QStringLiteral("bar:%1").arg(quintptr(bar),0,16),caption(bar->windowTitle()),{}};
            if (group.title.isEmpty()) group.title=bar->objectName();
            for (auto* action:bar->actions()) if (usable(action)) group.actions.append(action);
            if (!group.actions.isEmpty()) found.append(group);
        }
        visitedMenus.clear();
        if (cad->menuBar()) for (auto* action:cad->menuBar()->actions())
            if (action->menu()) collectMenu(action->menu(),caption(action->text()),found);
        QStringList next;
        for (const auto& group:found) {
            next.append(group.key+group.title);
            for (const auto& action:group.actions) next.append(QString::number(quintptr(action.data()),16));
        }
        if (signature!=next) {
            signature=next;catalog=found;
            const QString selected=groups->currentData().toString();
            QSignalBlocker blocker(groups);groups->clear();
            for (const auto& group:catalog) groups->addItem(group.title,group.key);
            const int previous=groups->findData(selected);
            if (previous>=0) groups->setCurrentIndex(previous);
            showGroup();
            clearRow(quickLayout);
            const QStringList pins={QStringLiteral("Std_New"),QStringLiteral("Std_Open"),
                QStringLiteral("Std_Save"),QStringLiteral("Std_Undo"),QStringLiteral("Std_Redo"),
                QStringLiteral("Std_ViewFitAll")};
            for (const auto& id:pins) {
                QAction* source=nullptr;
                for (const auto& group:catalog) {
                    for (const auto& action:group.actions)
                        if (action && action->objectName()==id) {source=action;break;}
                    if (source) break;
                }
                if (source) quickLayout->addWidget(mirror(source,quickContent,true));
            }
            quickLayout->addStretch();
            DrnasDiagnostics::note(QStringLiteral("ANDROID UI: original QAction groups=%1; source icons and command IDs retained")
                .arg(catalog.size()));
        }
        refreshing=false;
    }
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if (!cad || refreshing) return false;
        auto* widget=qobject_cast<QWidget*>(object);
        const bool upstream=widget && (widget==cad || cad->isAncestorOf(widget));
        const auto type=event->type();
        if (upstream && (type==QEvent::ActionAdded || type==QEvent::ActionRemoved ||
            type==QEvent::ChildAdded || type==QEvent::ChildRemoved ||
            (qobject_cast<QToolBar*>(widget) && (type==QEvent::Show || type==QEvent::Hide || type==QEvent::WindowTitleChange)) ||
            (qobject_cast<QMenuBar*>(widget) && type==QEvent::Show)))
            refreshTimer.start();
        return false;
    }
public:
    DrnasAndroidCommandUi(QMainWindow* shell,QMainWindow* original)
        :QWidget(shell),host(shell),cad(original) {
        setObjectName(QStringLiteral("drnasAndroidCommandUi"));
        setMinimumSize(0,0);setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
        outer->setSizeConstraint(QLayout::SetNoConstraint);
        auto* header=new QWidget(this);header->setObjectName(QStringLiteral("drnasAndroidHeader"));
        header->setStyleSheet(QStringLiteral(
            "QToolButton,QPushButton{min-width:48px;min-height:44px;padding:2px;}"));
        auto* row=new QHBoxLayout(header);row->setContentsMargins(4,2,4,2);row->setSpacing(4);
        quickContent=new QWidget(this);quickLayout=new QHBoxLayout(quickContent);
        quickLayout->setContentsMargins(0,0,0,0);quickLayout->setSpacing(2);
        quickLayout->setSizeConstraint(QLayout::SetMinimumSize);
        row->addWidget(scroller(quickContent,54),1);
        for (const auto& name:{QStringLiteral("drnasTouchCancel"),QStringLiteral("drnasTouchContext"),QStringLiteral("drnasTouchInputLauncher")}) {
            auto* source=host->findChild<QPushButton*>(name);
            if (!source) continue;
            auto* button=new QPushButton(source->text(),header);
            button->setObjectName(QStringLiteral("drnasAndroidHelper_")+name);
            button->setFocusPolicy(Qt::NoFocus);source->hide();
            connect(button,&QAbstractButton::clicked,source,&QAbstractButton::click);
            row->addWidget(button);
        }
        auto* menuButton=new QToolButton(header);menuButton->setText(QStringLiteral("Menu"));
        menuButton->setObjectName(QStringLiteral("drnasAndroidMenu"));
        menuButton->setPopupMode(QToolButton::InstantPopup);
        auto* menu=new QMenu(menuButton);menuButton->setMenu(menu);
        connect(menu,&QMenu::aboutToShow,this,[this,menuButton]{openMenu(menuButton);});
        row->addWidget(menuButton);outer->addWidget(header);
        auto* strip=new QWidget(this);strip->setObjectName(QStringLiteral("drnasAndroidToolStrip"));
        strip->setStyleSheet(QStringLiteral("QToolButton::menu-button{width:24px;}"));
        auto* stripLayout=new QHBoxLayout(strip);stripLayout->setContentsMargins(4,0,4,2);stripLayout->setSpacing(4);
        groups=new QComboBox(strip);groups->setObjectName(QStringLiteral("drnasAndroidGroups"));
        groups->setFixedWidth(156);groups->setMinimumHeight(48);
        groups->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        groups->setMinimumContentsLength(8);stripLayout->addWidget(groups);
        toolsContent=new QWidget(this);toolsLayout=new QHBoxLayout(toolsContent);
        toolsLayout->setContentsMargins(0,0,0,0);toolsLayout->setSpacing(4);
        toolsLayout->setSizeConstraint(QLayout::SetMinimumSize);
        stripLayout->addWidget(scroller(toolsContent,76),1);outer->addWidget(strip);
        original->setParent(this,Qt::Widget);original->setMinimumSize(0,0);
        original->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
        if (original->layout()) original->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        outer->addWidget(original,1);
        connect(groups,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{showGroup();});
        for (const auto& name:{QStringLiteral("drnasFloatingCommandsLauncher"),QStringLiteral("drnasDebugLauncher"),QStringLiteral("drnasLiveLabButton")})
            if (auto* launcher=host->findChild<QPushButton*>(name)) launcher->hide();
        refreshTimer.setSingleShot(true);refreshTimer.setInterval(60);
        connect(&refreshTimer,&QTimer::timeout,this,[this]{refresh();});
        host->setProperty("drnasAndroidCommandUiActive",true);
        qApp->installEventFilter(this);refreshTimer.start();
    }
};
