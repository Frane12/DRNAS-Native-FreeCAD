#include "../DrnasTouchInput.h"
#include <QApplication>
#include <QDialog>
#include <QHelpEvent>
#include <QMenu>
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
    new DrnasTouchInput(&host, cad, [](const QString&) {});
    QTest::qWait(40);

    QDialog modal(&host);
    modal.setWindowModality(Qt::ApplicationModal);
    modal.resize(220, 160);
    modal.move(host.mapToGlobal(QPoint(300, 150)));
    QPushButton inside("select", &modal);
    inside.move(30, 30); inside.show();
    QSignalSpy clickInside(&inside, &QPushButton::clicked);
    modal.show();
    QTest::qWait(40);
    check(QApplication::activeModalWidget() == &modal, "dialog must be active modal");
    QTest::mouseClick(&inside, Qt::LeftButton);
    check(clickInside.count() == 1, "original dialog control works");
    check(clickBehind.count() == 0, "dialog did not click CAD behind it");
    check(modal.isVisible() && QApplication::activeModalWidget() == &modal,
          "dialog remains modal");
    modal.hide();
    QTest::qWait(20);

    QMenu menu(&host);
    QAction* action = menu.addAction("View right");
    QSignalSpy chosen(action, &QAction::triggered);
    menu.popup(host.mapToGlobal(QPoint(100, 30)));
    QTest::qWait(30);
    check(QApplication::activePopupWidget() == &menu, "QMenu active");
    QPoint local = menu.actionGeometry(action).center();
    QHelpEvent help(QEvent::ToolTip, local, menu.mapToGlobal(local));
    QApplication::sendEvent(&menu, &help);
    check(seen("MENU_TOOLTIP suppressed"), "menu tooltip suppressed");
    check(menu.isVisible(), "menu remains open");
    QTest::mouseClick(&menu, Qt::LeftButton, Qt::NoModifier, local);
    check(chosen.count() == 1, "original action still triggers");
}
