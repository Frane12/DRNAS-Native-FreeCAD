// Reproduce the embedded FreeCAD show/raise ordering with real Qt widgets.
#include "../DrnasCameraControls.h"
#include "../DrnasTouchInput.h"
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
    int yaw = 0, pitch = 0;
    new DrnasCameraControls(&shell, cad, [&](int axis, int delta) {
        if (axis == 0) yaw += delta; else pitch += delta;
    });
    auto* left = shell.findChild<QPushButton*>("drnasRotateLeft");
    auto* right = shell.findChild<QPushButton*>("drnasRotateRight");
    auto* up = shell.findChild<QPushButton*>("drnasRotateUp");
    auto* down = shell.findChild<QPushButton*>("drnasRotateDown");
    check(left && right && up && down, "all four camera buttons must exist");

    // In the Android bootstrap, the embedded FreeCAD main window is shown
    // after the controls were constructed and can cover sibling widgets.
    cad->show();
    cad->raise();
    QTest::qWait(500);
    check(left->isVisible() && right->isVisible() && up->isVisible() && down->isVisible(), "camera buttons must remain visible");
    check(shell.childAt(left->geometry().center()) == left, "left button must be above CAD window");
    check(shell.childAt(right->geometry().center()) == right, "right button must be above CAD window");
    check(shell.childAt(up->geometry().center()) == up, "up button must be above CAD window");
    check(shell.childAt(down->geometry().center()) == down, "down button must be above CAD window");
    QTest::mouseClick(left, Qt::LeftButton);
    check(yaw == -1 && pitch == 0, "left button invokes negative yaw");
    QTest::mouseClick(right, Qt::LeftButton);
    check(yaw == 0 && pitch == 0, "right button invokes positive yaw");
    QTest::mouseClick(up, Qt::LeftButton);
    check(yaw == 0 && pitch == -1, "up button invokes negative pitch");
    QTest::mouseClick(down, Qt::LeftButton);
    check(yaw == 0 && pitch == 0, "down button invokes positive pitch");

    cad->hide();
    cad->show();
    cad->raise();
    QTest::qWait(30);
    check(shell.childAt(left->geometry().center()) == left, "buttons must return above re-shown CAD window");
    shell.resize(1000, 560);
    QTest::qWait(30);
    check(shell.childAt(right->geometry().center()) == right, "buttons remain clickable after resize");

    // Run actual Qt touch delivery with the application's relative-pointer filter installed.
    auto* quick = shell.addToolBar("Rotation");
    QList<QWidget*> controls;
    for (auto* arrow : {left, right, up, down}) {
        controls.append(arrow);
        controls.append(new DrnasRotationToolButton(quick, arrow));
    }
    new DrnasTouchInput(&shell, cad, [](const QString&) {});
    QTest::qWait(60);
    auto* device = QTest::createTouchDevice();
    const auto panelButton = [&shell](const QString& text) {
        for (auto* button : shell.findChildren<QPushButton*>())
            if (button->text() == text) return button;
        return static_cast<QPushButton*>(nullptr);
    };
    auto* hold = panelButton(QStringLiteral("Drži"));
    auto* rmb = panelButton(QStringLiteral("RMB"));
    check(hold && rmb, "relative mouse mode controls");
    hold->click();
    rmb->click();
    for (int i = 0; i < controls.size(); ++i) {
        auto* target = controls.at(i);
        int oldYaw = yaw, oldPitch = pitch;
        const bool horizontal = i < 4;
        const int direction = (i / 2) % 2 ? 1 : -1;
        auto center = target->rect().center();
        QTest::touchEvent(&shell, device).press(0, center, target);
        QTest::touchEvent(&shell, device).release(0, center, target);
        QTest::qWait(20);
        check(yaw == oldYaw + (horizontal ? direction : 0) &&
              pitch == oldPitch + (horizontal ? 0 : direction),
              "one physical tap must rotate exactly once with the touchpad active");
    }
    check(hold->isChecked() && rmb->isChecked(), "touch rotation preserves held RMB");
    rmb->click();
    hold->click();
    const int oldYaw = yaw;
    auto start = left->rect().center();
    QTest::touchEvent(&shell, device).press(0, start, left);
    QTest::touchEvent(&shell, device).move(0, start + QPoint(65, 0), left);
    QTest::touchEvent(&shell, device).release(0, left->rect().center(), left);
    QTest::qWait(20);
    check(yaw == oldYaw, "dragging a floating arrow must not rotate");
}
