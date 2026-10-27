// Keep virtual cursor motion and Qt menu hover in the same event turn.
#pragma once

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>

namespace DrnasMenuPointer {
inline bool sendHover(QMenu* menu, const QPointF& global, Qt::KeyboardModifiers modifiers) {
    if (!menu || !menu->isVisible()) return false;
    QPointer<QMenu> guarded(menu);
    const QPointF local(menu->mapFromGlobal(global.toPoint()));
    const QPointF window(menu->window()->mapFromGlobal(global.toPoint()));
    QMouseEvent event(QEvent::MouseMove, local, window, global,
                      Qt::NoButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(menu, &event);
    // The event may have closed or destroyed the popup.
    if (guarded && guarded->isVisible() && QApplication::activePopupWidget() == guarded.data()) {
        QAction* action = guarded->actionAt(local.toPoint());
        if (action && (!action->isEnabled() || !action->isVisible() || action->isSeparator()))
            action = nullptr;
        guarded->setActiveAction(action);
    }
    return true;
}
}
