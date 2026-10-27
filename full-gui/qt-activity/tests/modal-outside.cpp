#include "../DrnasTouchInput.h"
#include <QApplication>
#include <QDialog>
#include <QHelpEvent>
#include <QMenu>
#include <QPointingDevice>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

static QStringList trace;
namespace DrnasDiagnostics {
void note(const QString& message) { trace.append(message); }
}

static void check(bool ok, const char* what) { if (!ok) qFatal("%s", what); }
static bool seen(const char* label) {
    for (const auto& s : trace) if (s.contains(QString::fromLatin1(label))) return true;
    return false;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QMainWindow host;
    host.resize(800, 500);
    auto* cad = new QMainWindow(&host);
    host.setCentralWidget(cad);
    auto* behind = new QPushButton("behind", cad);
    behind->move(10, 10); behind->show();
    QSignalSpy clickBehind(behind, &QPushButton::clicked);
    host.show(); cad->show();
    QSettings().setValue(QStringLiteral("touch/speed"), 100);
    new DrnasTouchInput(&host, cad, [](const QString&) {});
    QTest::qWait(40);

    QDialog modal(&host);
    modal.setWindowModality(Qt::ApplicationModal);
    modal.resize(220, 160);
    modal.move(host.mapToGlobal(QPoint(300, 150)));
    modal.show();
    QTest::qWait(40);
    check(QApplication::activeModalWidget() == &modal, "dialog must be active modal");
    check(host.windowHandle(), "host window exists");
    const QPointF outside(host.mapToGlobal(QPoint(40, 40)));
    check(!modal.rect().contains(modal.mapFromGlobal(outside.toPoint())), "gesture starts outside modal");
    QPointingDevice touchDevice("test touch", 42, QInputDevice::DeviceType::TouchScreen,
        QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 4, 0);
    auto pointerGlobal = [&]() {
        for (auto* w : host.findChildren<QWidget*>()) {
            if (dynamic_cast<DrnasPointerMark*>(w) && w->isVisible())
                return w->mapToGlobal(QPoint(12, 12));
        }
        return QPoint(-9999, -9999);
    };
    const QPoint before = pointerGlobal();
    check(before.x() != -9999, "virtual pointer visible");
    QEventPoint pressed(7, QEventPoint::State::Pressed, outside, outside);
    QTouchEvent begin(QEvent::TouchBegin, &touchDevice, Qt::NoModifier, {pressed});
    QApplication::sendEvent(host.windowHandle(), &begin);
    if (!seen("MODAL_WINDOW outside touch begin")) {
        qWarning() << "Touch begin trace:" << trace;
        qFatal("outside touch began");
    }
    const QPointF moved = outside + QPointF(25, 12);
    QEventPoint updated(7, QEventPoint::State::Updated, moved, moved);
    QTouchEvent move(QEvent::TouchUpdate, &touchDevice, Qt::NoModifier, {updated});
    QApplication::sendEvent(host.windowHandle(), &move);
    const QPoint after = pointerGlobal();
    if (!seen("MODAL_WINDOW outside touch move") || after - before != QPoint(25, 12)) {
        qWarning() << "Pointer before/after:" << before << after << "trace:" << trace;
        qFatal("pointer follows outside gesture delta");
    }
    QEventPoint released(7, QEventPoint::State::Released, moved, moved);
    QTouchEvent end(QEvent::TouchEnd, &touchDevice, Qt::NoModifier, {released});
    QApplication::sendEvent(host.windowHandle(), &end);
    check(seen("MODAL_WINDOW outside touch begin"), "outside touch began");
    check(seen("MODAL_WINDOW outside touch move"), "outside touch moved pointer");
    check(seen("MODAL_WINDOW outside touch end"), "outside touch ended");
    check(clickBehind.count() == 0, "gesture did not click CAD behind modal");
    check(modal.isVisible() && QApplication::activeModalWidget() == &modal,
          "dialog remains modal and open");
    modal.hide();
    QTest::qWait(20);

    QMenu menu(&host);
    menu.addAction("View right");
    menu.popup(host.mapToGlobal(QPoint(100, 30)));
    QTest::qWait(30);
    check(QApplication::activePopupWidget() == &menu, "QMenu active");
    QPoint local = menu.rect().center();
    QHelpEvent help(QEvent::ToolTip, local, menu.mapToGlobal(local));
    QApplication::sendEvent(&menu, &help);
    check(seen("MENU_TOOLTIP suppressed"), "only menu tooltip suppressed");
    check(menu.isVisible(), "menu remains open");
}
