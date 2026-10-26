#pragma once

#include <QAction>
#include <QObject>
#include <QPointer>
#include <QVariant>

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
