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
    QPointer<QPushButton> up_;
    QPointer<QPushButton> down_;

    void raiseButtons() {
        if (left_ && !left_->isHidden()) left_->raise();
        if (right_ && !right_->isHidden()) right_->raise();
        if (up_ && !up_->isHidden()) up_->raise();
        if (down_ && !down_->isHidden()) down_->raise();
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
    DrnasCameraControls(QMainWindow* shell, QMainWindow* cad, std::function<void(int, int)> rotate)
        : QObject(shell), shell_(shell), cad_(cad) {
        struct ButtonSpec {
            const char* text;
            const char* name;
            const char* key;
            const char* tip;
            int axis;
            int direction;
            double x;
            double y;
            QPointer<QPushButton>* slot;
        };
        const ButtonSpec buttons[] = {
            {"↶", "drnasRotateLeft", "drnasOverlay/rotateLeft", "Zakreni pogled ulijevo", 0, -1, 0.38, 0.78, &left_},
            {"↷", "drnasRotateRight", "drnasOverlay/rotateRight", "Zakreni pogled udesno", 0, 1, 0.47, 0.78, &right_},
            {"↑", "drnasRotateUp", "drnasOverlay/rotateUp", "Nagni pogled gore", 1, -1, 0.425, 0.64, &up_},
            {"↓", "drnasRotateDown", "drnasOverlay/rotateDown", "Nagni pogled dolje", 1, 1, 0.425, 0.90, &down_},
        };
        for (const auto& spec : buttons) {
            const QString key = QString::fromLatin1(spec.key);
            auto* button = new QPushButton(QString::fromUtf8(spec.text), shell);
            button->setObjectName(QString::fromLatin1(spec.name));
            button->setToolTip(QString::fromUtf8(spec.tip));
            button->setFixedSize(48, 42);
            button->setStyleSheet(QStringLiteral(
                "QPushButton{background:#315775;color:white;border:1px solid #6b91ad;"
                "border-radius:5px;font-size:24px;} QPushButton:pressed{background:#4383a9;}"));
            QSettings settings;
            if (!settings.contains(key + QStringLiteral("/x")))
                settings.setValue(key + QStringLiteral("/x"), spec.x);
            if (!settings.contains(key + QStringLiteral("/y")))
                settings.setValue(key + QStringLiteral("/y"), spec.y);
            new DrnasDraggableLauncher(shell, button, key, 0, false, this);
            QObject::connect(button, &QPushButton::clicked, this,
                             [rotate, axis = spec.axis, direction = spec.direction] { rotate(axis, direction); });
            *spec.slot = button;
            button->show();
        }
        shell->installEventFilter(this);
        if (cad) cad->installEventFilter(this);
        queueRaise();
        QTimer::singleShot(450, this, [this] { raiseButtons(); });
    }
};
