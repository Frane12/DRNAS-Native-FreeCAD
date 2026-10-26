// Reproduce the embedded FreeCAD show/raise ordering with real Qt widgets.
#include "../DrnasCameraControls.h"
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

static void check(bool value, const char* message) { if (!value) qFatal("%s", message); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir settingsDir;
    check(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QCoreApplication::setOrganizationName("DrnasCameraTest");
    QCoreApplication::setApplicationName("Controls");

    QMainWindow shell;
    shell.resize(900, 500);
    shell.show();
    auto* cad = new QMainWindow(&shell);
    shell.setCentralWidget(cad);
    int result = 0;
    new DrnasCameraControls(&shell, cad, [&](int delta) { result += delta; });
    auto* left = shell.findChild<QPushButton*>("drnasRotateLeft");
    auto* right = shell.findChild<QPushButton*>("drnasRotateRight");
    check(left && right, "both camera buttons must exist");

    // In the Android bootstrap, the embedded FreeCAD main window is shown
    // after the controls were constructed and can cover sibling widgets.
    cad->show();
    cad->raise();
    QTest::qWait(500);
    check(left->isVisible() && right->isVisible(), "camera buttons must remain visible");
    check(shell.childAt(left->geometry().center()) == left, "left button must be above CAD window");
    check(shell.childAt(right->geometry().center()) == right, "right button must be above CAD window");
    QTest::mouseClick(left, Qt::LeftButton);
    check(result == -1, "left button invokes negative rotation");
    QTest::mouseClick(right, Qt::LeftButton);
    check(result == 0, "right button invokes positive rotation");

    cad->hide();
    cad->show();
    cad->raise();
    QTest::qWait(30);
    check(shell.childAt(left->geometry().center()) == left, "buttons must return above re-shown CAD window");
    shell.resize(1000, 560);
    QTest::qWait(30);
    check(shell.childAt(right->geometry().center()) == right, "buttons remain clickable after resize");
}
