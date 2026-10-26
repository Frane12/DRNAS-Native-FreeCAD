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
inline int drnasRouteDocumentOpen(QObject* window, QAction* androidOpen)
{
    int count = 0;
    for (auto* action : window->findChildren<QAction*>(QStringLiteral("Std_Open"))) {
        if (action->property("drnasAndroidOpenRouted").toBool()) continue;
        QObject* owner = action->parent();
        if (!owner || !QObject::disconnect(action, SIGNAL(triggered(bool)), owner, nullptr))
            continue;
        const QPointer<QAction> target(androidOpen);
        QObject::connect(action, &QAction::triggered, action, [target] {
            if (target) target->trigger();
        });
        action->setProperty("drnasAndroidOpenRouted", true);
        ++count;
    }
    return count;
}

// The embedded MainWindow is installed BEFORE FreeCAD creates its standard
// actions. Watch later toolbar/menu construction and workbench rebuilds, and
// bind synchronously before a user's click/shortcut can activate the old path.
class DrnasDocumentOpenRouter final : public QObject {
    QPointer<QObject> window_;
    QPointer<QAction> target_;
    QTimer retry_;
    bool queued_ = false;
    std::function<void(int)> report_;

    void bind() {
        if (!window_ || !target_) return;
        const int count = drnasRouteDocumentOpen(window_, target_);
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
    DrnasDocumentOpenRouter(QObject* window, QAction* target,
                            std::function<void(int)> report = {})
        : QObject(window), window_(window), target_(target), retry_(this),
          report_(std::move(report)) {
        QCoreApplication::instance()->installEventFilter(this);
        QObject::connect(&retry_, &QTimer::timeout, this, [this] { bind(); });
        retry_.start(250);
        bind();
    }
};
