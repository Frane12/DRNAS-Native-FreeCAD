// Test real Qt QMenu hover and action dispatch with virtual global coordinates.
#include "../DrnasMenuPointer.h"
#include <QMainWindow>
#include <QTest>
#include <QSignalSpy>

static void check(bool ok, const char* what) { if (!ok) qFatal("%s", what); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QMainWindow host;
    host.resize(600, 400);
    host.show();
    QMenu menu(&host);
    QAction* first = menu.addAction("Isometric");
    QAction* top = menu.addAction("Top");
    QAction* disabled = menu.addAction("Unavailable");
    disabled->setEnabled(false);
    QSignalSpy selected(top, &QAction::triggered);
    menu.popup(host.mapToGlobal(QPoint(80, 50)));
    QTest::qWait(30);
    check(menu.isVisible(), "menu visible");
    check(QApplication::activePopupWidget() == &menu, "menu owns popup");
    auto globalCenter = [&](QAction* a) { return QPointF(menu.mapToGlobal(menu.actionGeometry(a).center())); };
    check(DrnasMenuPointer::sendHover(&menu, globalCenter(first), Qt::NoModifier), "first hover sent");
    check(menu.activeAction() == first, "first hover follows pointer");
    check(DrnasMenuPointer::sendHover(&menu, globalCenter(top), Qt::NoModifier), "second hover sent");
    check(menu.activeAction() == top, "second hover follows pointer");
    check(DrnasMenuPointer::sendHover(&menu, globalCenter(disabled), Qt::NoModifier), "disabled hover sent");
    check(menu.activeAction() == nullptr, "disabled action is not active");
    check(DrnasMenuPointer::sendHover(&menu, QPointF(menu.mapToGlobal(QPoint(-20, -20))), Qt::NoModifier), "outside hover sent");
    check(menu.activeAction() == nullptr, "outside pointer clears highlight");
    DrnasMenuPointer::sendHover(&menu, globalCenter(top), Qt::NoModifier);
    QTest::mouseClick(&menu, Qt::LeftButton, Qt::NoModifier, menu.actionGeometry(top).center());
    check(selected.count() == 1, "normal menu click still triggers selected action");
    check(!DrnasMenuPointer::sendHover(nullptr, QPointF(), Qt::NoModifier), "null menu ignored");
}
