#pragma once

// Drag handles for the four DRNAS launcher buttons only. This code moves
// ordinary child QPushButtons in QtActivity's existing QMainWindow. It never
// changes FreeCAD toolbars, the Coin3D widget, EGL or the native window size.
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QTouchEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <utility>

class DrnasDraggableLauncher final : public QObject {
    QPointer<QWidget> host_;
    QPointer<QPushButton> button_;
    QString key_;
    double x_, y_;
    bool tracking_ = false;
    bool dragging_ = false;
    bool touch_ = false;
    int touchId_ = -1;
    QPoint startGlobal_;
    QPoint startLocal_;
    QElapsedTimer lastTouch_;

    static double normalized(double value) {
        if (!std::isfinite(value)) return 0.0;
        return std::clamp(value, 0.0, 1.0);
    }
    QPoint clamped(const QPoint& desired) const {
        if (!host_ || !button_) return {};
        return QPoint(std::clamp(desired.x(), 0, std::max(0, host_->width() - button_->width())),
                      std::clamp(desired.y(), 0, std::max(0, host_->height() - button_->height())));
    }
    void save() {
        if (!host_ || !button_) return;
        const int availX = std::max(1, host_->width() - button_->width());
        const int availY = std::max(1, host_->height() - button_->height());
        x_ = normalized(double(button_->x()) / availX);
        y_ = normalized(double(button_->y()) / availY);
        QSettings settings;
        settings.setValue(key_ + QStringLiteral("/x"), x_);
        settings.setValue(key_ + QStringLiteral("/y"), y_);
        settings.sync();
    }
    void begin(const QPoint& global, bool touch) {
        startGlobal_ = global;
        startLocal_ = button_ ? button_->pos() : QPoint();
        tracking_ = true;
        dragging_ = false;
        touch_ = touch;
    }
    void advance(const QPoint& global) {
        if (!tracking_ || !button_) return;
        const QPoint diff = global - startGlobal_;
        if (!dragging_ && diff.manhattanLength() < std::max(10, QApplication::startDragDistance()))
            return;
        if (!dragging_) {
            dragging_ = true;
            button_->setDown(false); // prevents a drag from activating Tools/LAB/DBG/Mis
        }
        button_->move(clamped(startLocal_ + diff));
        button_->raise();
    }
    bool finish(bool touch) {
        if (!tracking_ || touch_ != touch) return false;
        const bool moved = dragging_;
        tracking_ = dragging_ = false;
        touchId_ = -1;
        if (moved) save();
        // Native mouse taps must keep the pressed state until QPushButton
        // handles MouseButtonRelease and emits clicked(). Touch is handled here.
        if (button_ && (moved || touch)) button_->setDown(false);
        return moved;
    }

protected:
    bool eventFilter(QObject* target, QEvent* e) override {
        if (!host_ || !button_ || !e) return false;
        if (target == host_ && e->type() == QEvent::Resize) {
            // Recalculate from normalized coordinates on rotation/size changes.
            place();
            return false;
        }
        if (target != button_) return false;
        const auto type = e->type();
        if (type == QEvent::TouchBegin || type == QEvent::TouchUpdate ||
            type == QEvent::TouchEnd || type == QEvent::TouchCancel) {
            lastTouch_.restart();
            if (type == QEvent::TouchCancel) {
                finish(true); return true;
            }
            auto* t = static_cast<QTouchEvent*>(e);
            if (t->points().isEmpty()) return true;
            const auto& p = t->points().first();
            if (type == QEvent::TouchBegin) {
                if (t->points().size() != 1) return true;
                touchId_ = p.id();
                begin(p.globalPosition().toPoint(), true);
                button_->setDown(true);
                return true;
            }
            if (!tracking_ || !touch_ || p.id() != touchId_) return true;
            advance(p.globalPosition().toPoint());
            if (type == QEvent::TouchEnd) {
                const bool moved = finish(true);
                if (!moved && button_) button_->click();
            }
            return true; // consume touch stream; never translate it into CAD input
        }
        if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick ||
            type == QEvent::MouseMove || type == QEvent::MouseButtonRelease) {
            auto* m = static_cast<QMouseEvent*>(e);
            // Some Qt Android builds send a synthetic mouse event after touch.
            if (lastTouch_.isValid() && lastTouch_.elapsed() < 450 &&
                m->source() != Qt::MouseEventNotSynthesized) return true;
            if ((type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) &&
                m->button() == Qt::LeftButton) {
                begin(m->globalPosition().toPoint(), false);
                return false; // a normal tap/click remains a native QPushButton click
            }
            if (type == QEvent::MouseMove && tracking_ && !touch_) {
                advance(m->globalPosition().toPoint());
                return dragging_;
            }
            if (type == QEvent::MouseButtonRelease && tracking_ && !touch_) {
                if (m->button() != Qt::LeftButton) return false;
                return finish(false); // swallow release only after an actual drag
            }
        }
        if (type == QEvent::Hide) {
            tracking_ = dragging_ = false;
            touchId_ = -1;
        }
        return false;
    }

public:
    // bottomInset is measured up from the bottom edge, so initial positions
    // of DBG, LAB, Tools and Mis never overlap before customization.
    DrnasDraggableLauncher(QWidget* host, QPushButton* button, QString key,
                           int bottomInset, bool right, QObject* parent = nullptr)
        : QObject(parent ? parent : host), host_(host), button_(button), key_(std::move(key)) {
        if (!host_ || !button_) return;
        QSettings settings;
        const double initialX = right ? normalized(double(std::max(0, host_->width() -
                                      button_->width() - 5)) /
                                      std::max(1, host_->width() - button_->width()))
                                      : normalized(5.0 / std::max(1, host_->width() - button_->width()));
        const double initialY = normalized(double(std::max(0, host_->height() -
                                      button_->height() - bottomInset)) /
                                      std::max(1, host_->height() - button_->height()));
        x_ = normalized(settings.value(key_ + QStringLiteral("/x"), initialX).toDouble());
        y_ = normalized(settings.value(key_ + QStringLiteral("/y"), initialY).toDouble());
        button_->setAttribute(Qt::WA_AcceptTouchEvents, true);
        button_->setFocusPolicy(Qt::NoFocus);
        button_->installEventFilter(this);
        host_->installEventFilter(this);
        place();
    }

    void place() {
        if (!host_ || !button_ || dragging_) return;
        const int availX = std::max(0, host_->width() - button_->width());
        const int availY = std::max(0, host_->height() - button_->height());
        button_->move(clamped(QPoint(qRound(x_ * availX), qRound(y_ * availY))));
        button_->raise();
    }
};
