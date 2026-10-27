#include "../DrnasTouchInput.h"
#include <QApplication>
#include <QCheckBox>
#include <QMouseEvent>
#include <QSettings>
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
    QCoreApplication::setOrganizationName("DRNAS-Test");
    QCoreApplication::setApplicationName("touch-mode");
    QSettings().remove("touch/relativeMode");
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

    auto* mode = host.findChild<QCheckBox*>("drnasRelativeMouseMode");
    check(mode && !mode->isChecked(), "direct touch is default");
    QPushButton touchButton("touch", cad);
    touchButton.setGeometry(150, 10, 90, 30);
    touchButton.show();
    const QPointF touchLocal(20, 15);
    const QPointF global = touchButton.mapToGlobal(touchLocal.toPoint());
    const QPointF window = touchButton.window()->mapFromGlobal(global.toPoint());
    auto sendSynthetic = [&](QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, touchLocal, window, global, button, buttons,
                          Qt::NoModifier, Qt::MouseEventSynthesizedBySystem);
        qInfo("touch test type=%d source=%d mode=%d down_before=%d",
              int(type), int(event.source()), int(mode->isChecked()), int(touchButton.isDown()));
        QApplication::sendEvent(&touchButton, &event);
        qInfo("touch test down_after=%d", int(touchButton.isDown()));
    };
    sendSynthetic(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
    check(touchButton.isDown(), "direct touch reaches target control");
    sendSynthetic(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
    check(!touchButton.isDown(), "direct touch release completed");
    mode->setChecked(true);
    check(seen("INPUT mode relative mouse"), "relative mode enabled");
    sendSynthetic(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
    check(!touchButton.isDown(), "relative mode captures synthetic input");
    sendSynthetic(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
    mode->setChecked(false);
    QTest::qWait(30);
    QSettings().remove("touch/relativeMode");
    touchButton.hide();

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
