#pragma once
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include "StartupDiagnostics.h"

// All LAB surfaces are ordinary child widgets. Never create a second Android
// window or OpenGL surface, and never consume or synthesize input events.
class DrnasLabOutline : public QWidget {
public:
    QPointer<QWidget> target;
    explicit DrnasLabOutline(QWidget* host) : QWidget(host) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }
    void paintEvent(QPaintEvent*) override {
        if (!target || !target->isVisible()) return;
        QPainter p(this);
        p.setPen(QPen(QColor("#ffca28"), 2));
        const QRect r(mapFromGlobal(target->mapToGlobal(QPoint())), target->size());
        p.drawRect(r.adjusted(1, 1, -2, -2));
        p.drawText(r.topLeft() + QPoint(4, 16), QStringLiteral("LAB target"));
    }
};

class DrnasLiveGuiLab : public QObject {
    QMainWindow* host;
    QPushButton* launcher;
    QScrollArea* panel;
    DrnasLabOutline* outline;
    QLabel* info;
    QCheckBox* borders;
    QComboBox* placement;
    QSpinBox *height, *fontSize, *iconSize;
    QPointer<QToolBar> bar;
    QString originalStyle, lastTouch = QStringLiteral("No touch recorded");
    QSize originalIcons, originalMinimum, originalMaximum;
    Qt::ToolBarAreas originalAllowed;
    Qt::ToolBarArea originalArea = Qt::TopToolBarArea;
    bool ready = false;

    static QString describe(QWidget* w) {
        if (!w) return QStringLiteral("none");
        const QPoint g = w->mapToGlobal(QPoint());
        return QStringLiteral("%1/%2 global=(%3,%4) size=%5x%6 visible=%7 enabled=%8 window=%9")
            .arg(QString::fromLatin1(w->metaObject()->className()), w->objectName())
            .arg(g.x()).arg(g.y()).arg(w->width()).arg(w->height())
            .arg(w->isVisible()).arg(w->isEnabled()).arg(w->isWindow());
    }
    QString report() const {
        QString s = QStringLiteral("DRNAS Live GUI LAB (logical Qt pixels)\n")
            + QStringLiteral("Host: ") + describe(host)
            + QStringLiteral("\nWorkspace: ") + describe(host->centralWidget())
            + QStringLiteral("\nToolbar: ") + describe(bar.data())
            + QStringLiteral("\nToolbar parent: ") + describe(bar ? bar->parentWidget() : nullptr)
            + QStringLiteral("\nDPR=%1 position=%2 height=%3 font=%4 icons=%5\n")
                .arg(host->devicePixelRatioF()).arg(placement->currentText())
                .arg(height->value()).arg(fontSize->value()).arg(iconSize->value())
            + lastTouch;
        if (bar) for (QWidget* w : bar->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
            s += QStringLiteral("\n  ") + describe(w);
        return s;
    }
    void arrange() {
        const QRect r = host->contentsRect();
        const int w = std::max(1, std::min(440, r.width() - 12));
        const int h = std::max(1, std::min(330, r.height() - 64));
        launcher->setGeometry(r.right() - 77, r.bottom() - 47, 72, 42);
        panel->setGeometry(r.right() - w - 5, r.bottom() - h - 53, w, h);
        outline->setGeometry(host->rect());
        outline->raise(); panel->raise(); launcher->raise();
    }
    void apply() {
        if (!ready || !bar) return;
        const auto area = placement->currentIndex() == 0 ? Qt::TopToolBarArea : Qt::BottomToolBarArea;
        bar->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
        if (host->toolBarArea(bar) != area) host->addToolBar(area, bar);
        bar->setMinimumHeight(height->value() ? height->value() : originalMinimum.height());
        bar->setMaximumHeight(height->value() ? height->value() : originalMaximum.height());
        bar->setIconSize(QSize(iconSize->value(), iconSize->value()));
        bar->setStyleSheet(originalStyle + QStringLiteral("\nQToolButton{font-size:%1px;}").arg(fontSize->value()));
        bar->updateGeometry();
        DrnasDiagnostics::note(QStringLiteral("LAB applied: position=%1 height=%2 font=%3 icons=%4")
            .arg(placement->currentText()).arg(height->value()).arg(fontSize->value()).arg(iconSize->value()));
    }
    void reset() {
        if (!bar) return;
        const QSignalBlocker a(placement), b(height), c(fontSize), d(iconSize);
        bar->setAllowedAreas(Qt::AllToolBarAreas);
        host->addToolBar(originalArea, bar);
        bar->setAllowedAreas(originalAllowed);
        bar->setMinimumSize(originalMinimum); bar->setMaximumSize(originalMaximum);
        bar->setIconSize(originalIcons); bar->setStyleSheet(originalStyle);
        placement->setCurrentIndex(originalArea == Qt::BottomToolBarArea ? 1 : 0);
        height->setValue(0); fontSize->setValue(12); iconSize->setValue(originalIcons.width());
        borders->setChecked(false);
        DrnasDiagnostics::note(QStringLiteral("LAB restored original toolbar settings"));
    }
protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (e->type() == QEvent::MouseButtonPress) {
            auto* w = qobject_cast<QWidget*>(obj);
            if (w && w != launcher && w != panel && !panel->isAncestorOf(w)
                && (w == host || host->isAncestorOf(w))) {
                const QPoint p = static_cast<QMouseEvent*>(e)->globalPosition().toPoint();
                // Keep the deepest receiver; propagated copies have the same event.
                if (QApplication::widgetAt(p) == w)
                    lastTouch = QStringLiteral("Touch global=(%1,%2) receiver: %3")
                        .arg(p.x()).arg(p.y()).arg(describe(w));
            }
        }
        return false;
    }
public:
    explicit DrnasLiveGuiLab(QMainWindow* window) : QObject(window), host(window) {
        launcher = new QPushButton(QStringLiteral("LAB"), host);
        launcher->setObjectName(QStringLiteral("drnasLiveLabButton"));
        launcher->setStyleSheet(QStringLiteral("background:#304c65;color:white;border:1px solid #91b4cc;border-radius:6px;"));
        panel = new QScrollArea(host);
        panel->setObjectName(QStringLiteral("drnasLiveLabPanel"));
        panel->setWidgetResizable(true);
        auto* body = new QWidget;
        auto* layout = new QVBoxLayout(body);
        auto* title = new QLabel(QStringLiteral("LIVE GUI LAB — changes last until restart"));
        title->setWordWrap(true); layout->addWidget(title);
        auto* form = new QFormLayout;
        placement = new QComboBox; placement->addItems({QStringLiteral("Top"), QStringLiteral("Bottom")});
        height = new QSpinBox; height->setRange(0, 160); height->setSpecialValueText(QStringLiteral("Auto"));
        fontSize = new QSpinBox; fontSize->setRange(8, 24); fontSize->setValue(12);
        iconSize = new QSpinBox; iconSize->setRange(12, 48); iconSize->setValue(21);
        form->addRow(QStringLiteral("Toolbar position"), placement);
        form->addRow(QStringLiteral("Toolbar height (px)"), height);
        form->addRow(QStringLiteral("Button text (px)"), fontSize);
        form->addRow(QStringLiteral("Icons (px)"), iconSize); layout->addLayout(form);
        borders = new QCheckBox(QStringLiteral("Show toolbar bounds")); layout->addWidget(borders);
        info = new QLabel; info->setWordWrap(true); info->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(info);
        auto* row = new QHBoxLayout;
        auto* copy = new QPushButton(QStringLiteral("Copy measurements"));
        auto* restore = new QPushButton(QStringLiteral("Reset"));
        auto* close = new QPushButton(QStringLiteral("Close"));
        row->addWidget(copy); row->addWidget(restore); row->addWidget(close); layout->addLayout(row);
        panel->setWidget(body);
        panel->setStyleSheet(QStringLiteral("QScrollArea{background:#233343;border:1px solid #91b4cc;} QPushButton,QSpinBox,QComboBox{min-height:32px;}"));
        outline = new DrnasLabOutline(host); outline->hide(); panel->hide();
        connect(launcher, &QPushButton::clicked, this, [this] { panel->setVisible(!panel->isVisible()); arrange(); });
        connect(close, &QPushButton::clicked, panel, &QWidget::hide);
        connect(restore, &QPushButton::clicked, this, [this] { reset(); });
        connect(copy, &QPushButton::clicked, this, [this] { const QString s = report(); QApplication::clipboard()->setText(s); DrnasDiagnostics::note(s); });
        connect(placement, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { apply(); });
        for (auto* spin : {height, fontSize, iconSize}) connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { apply(); });
        connect(borders, &QCheckBox::toggled, outline, &QWidget::setVisible);
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this, restore] {
            if (!bar) {
                ready = false;
                bar = host->findChild<QToolBar*>(QStringLiteral("drnasAndroidFreeCADCommands"));
                if (bar) {
                    originalStyle = bar->styleSheet(); originalIcons = bar->iconSize();
                    originalMinimum = bar->minimumSize(); originalMaximum = bar->maximumSize();
                    originalArea = host->toolBarArea(bar); originalAllowed = bar->allowedAreas();
                    if (originalArea == Qt::NoToolBarArea) { bar = nullptr; }
                    else { outline->target = bar; ready = true; }
                }
            }
            for (QWidget* control : QList<QWidget*>{placement, height, fontSize, iconSize, restore}) control->setEnabled(ready);
            if (panel->isVisible()) info->setText(ready ? report() : QStringLiteral("Open original FreeCAD GUI first."));
            arrange(); if (outline->isVisible()) outline->update();
        });
        qApp->installEventFilter(this);
        launcher->show(); arrange(); timer->start(250);
    }
};
