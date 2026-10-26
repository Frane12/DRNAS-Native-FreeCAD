#pragma once

#include <QAction>
#include <QObject>
#include <QPointer>
#include <QVariant>
#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <functional>
#include <utility>

// Keep FreeCAD's existing QAction, icon, shortcut and menu/toolbar placement.
// Only its Gui::Action activation receiver is replaced with the Android picker.
// Repeated workbench/layout setup must not add duplicate handlers.
inline int drnasRouteDocumentAction(QObject* window, QAction* targetAction, const QString& command)
{
    int count = 0;
    for (auto* action : window->findChildren<QAction*>(command)) {
        if (action->property("drnasAndroidActionRouted").toBool()) continue;
        QObject* owner = action->parent();
        if (!owner || !QObject::disconnect(action, SIGNAL(triggered(bool)), owner, nullptr))
            continue;
        const QPointer<QAction> target(targetAction);
        QObject::connect(action, &QAction::triggered, action, [target] {
            if (target) target->trigger();
        });
        action->setProperty("drnasAndroidActionRouted", true);
        ++count;
    }
    return count;
}

inline int drnasRouteDocumentOpen(QObject* window, QAction* target) {
    return drnasRouteDocumentAction(window, target, QStringLiteral("Std_Open"));
}

// The embedded MainWindow is installed BEFORE FreeCAD creates its standard
// actions. Watch later toolbar/menu construction and workbench rebuilds, and
// bind synchronously before a user's click/shortcut can activate the old path.
class DrnasDocumentActionRouter final : public QObject {
    QPointer<QObject> window_;
    QPointer<QAction> target_;
    QTimer retry_;
    bool queued_ = false;
    std::function<void(int)> report_;
    QString command_;

    void bind() {
        if (!window_ || !target_) return;
        const int count = drnasRouteDocumentAction(window_, target_, command_);
        if (count > 0) {
            retry_.stop();
            if (report_) report_(count);
        }
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::ChildAdded:
        case QEvent::ActionAdded:
            if (!queued_) {
                queued_ = true;
                QTimer::singleShot(0, this, [this] { queued_ = false; bind(); });
            }
            break;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::TouchBegin:
        case QEvent::Shortcut:
            bind();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }
public:
    DrnasDocumentActionRouter(QObject* window, QAction* target,
                            std::function<void(int)> report = {},
                            QString command = QStringLiteral("Std_Open"))
        : QObject(window), window_(window), target_(target), retry_(this),
          report_(std::move(report)), command_(std::move(command)) {
        QCoreApplication::instance()->installEventFilter(this);
        QObject::connect(&retry_, &QTimer::timeout, this, [this] { bind(); });
        retry_.start(250);
        bind();
    }
};

using DrnasDocumentOpenRouter = DrnasDocumentActionRouter;
