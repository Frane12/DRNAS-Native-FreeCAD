#pragma once

#include <functional>
#include <QEvent>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include "DrnasDraggableLauncher.h"

// Child widgets of the existing QtActivity shell; no additional GL surface.
class DrnasCameraControls final : public QObject {
    QPointer<QMainWindow> shell_;
    QPointer<QMainWindow> cad_;
    QPointer<QPushButton> left_;
    QPointer<QPushButton> right_;

    void raiseButtons() {
        if (left_ && !left_->isHidden()) left_->raise();
        if (right_ && !right_->isHidden()) right_->raise();
    }
    void queueRaise() {
        QTimer::singleShot(0, this, [this] { raiseButtons(); });
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if ((object == cad_.data() && (event->type() == QEvent::Show || event->type() == QEvent::Resize)) ||
            (object == shell_.data() && (event->type() == QEvent::Show || event->type() == QEvent::WindowActivate)))
            queueRaise();
        return QObject::eventFilter(object, event);
    }

public:
    DrnasCameraControls(QMainWindow* shell, QMainWindow* cad, std::function<void(int)> rotate)
        : QObject(shell), shell_(shell), cad_(cad) {
        for (int direction : {-1, 1}) {
            const bool left = direction < 0;
            const QString key = left ? QStringLiteral("drnasOverlay/rotateLeft")
                                     : QStringLiteral("drnasOverlay/rotateRight");
            auto* button = new QPushButton(left ? QStringLiteral("↶") : QStringLiteral("↷"), shell);
            button->setObjectName(left ? QStringLiteral("drnasRotateLeft")
                                       : QStringLiteral("drnasRotateRight"));
            button->setToolTip(left ? QStringLiteral("Zakreni pogled ulijevo")
                                    : QStringLiteral("Zakreni pogled udesno"));
            button->setFixedSize(48, 42);
            button->setStyleSheet(QStringLiteral(
                "QPushButton{background:#315775;color:white;border:1px solid #6b91ad;"
                "border-radius:5px;font-size:24px;} QPushButton:pressed{background:#4383a9;}"));
            QSettings settings;
            if (!settings.contains(key + QStringLiteral("/x")))
                settings.setValue(key + QStringLiteral("/x"), left ? 0.38 : 0.47);
            if (!settings.contains(key + QStringLiteral("/y")))
                settings.setValue(key + QStringLiteral("/y"), 0.78);
            new DrnasDraggableLauncher(shell, button, key, 0, false, this);
            QObject::connect(button, &QPushButton::clicked, this, [rotate, direction] { rotate(direction); });
            if (left) left_ = button; else right_ = button;
            button->show();
        }
        shell->installEventFilter(this);
        if (cad) cad->installEventFilter(this);
        queueRaise();
        QTimer::singleShot(450, this, [this] { raiseButtons(); });
    }
};
