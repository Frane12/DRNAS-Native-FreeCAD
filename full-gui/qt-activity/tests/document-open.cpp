#include "../DrnasDocumentOpen.h"
#include <QApplication>
#include <QMenu>
#include <QToolBar>
#include <QtTest>

class DocumentOpenTests : public QObject {
    Q_OBJECT
private slots:
    void routeExistingActionOnce() {
        QObject window;
        QObject owner(&window);
        QAction action(&owner), other(&owner), target(&window);
        action.setObjectName(QStringLiteral("Std_Open"));
        action.setText(QStringLiteral("Open"));
        action.setShortcut(QKeySequence::Open);
        QMenu menu;
        QToolBar bar;
        menu.addAction(&action);
        bar.addAction(&action);
        int nativeCalls = 0, otherCalls = 0, androidCalls = 0;
        QObject::connect(&action, &QAction::triggered, &owner, [&] { ++nativeCalls; });
        QObject::connect(&other, &QAction::triggered, &owner, [&] { ++otherCalls; });
        QObject::connect(&target, &QAction::triggered, &window, [&] { ++androidCalls; });
        QSignalSpy observed(&action, &QAction::triggered);
        QCOMPARE(drnasRouteDocumentOpen(&window, &target), 1);
        QCOMPARE(drnasRouteDocumentOpen(&window, &target), 0);
        action.trigger();
        other.trigger();
        QCOMPARE(nativeCalls, 0);
        QCOMPARE(androidCalls, 1);
        QCOMPARE(otherCalls, 1);
        QCOMPARE(observed.count(), 1);
        QCOMPARE(action.shortcut(), QKeySequence(QKeySequence::Open));
        QCOMPARE(menu.actions().front(), &action);
        QCOMPARE(bar.actions().front(), &action);
        target.setEnabled(false);
        action.trigger();
        QCOMPARE(androidCalls, 1);
    }
};
QTEST_MAIN(DocumentOpenTests)
#include "document-open.moc"
