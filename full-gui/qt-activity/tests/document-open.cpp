#include "../DrnasDocumentOpen.h"
#include <QApplication>
#include <QMenu>
#include <QToolBar>
#include <QtTest>

class DocumentOpenTests : public QObject {
    Q_OBJECT
private slots:
    void routeActionCreatedAfterRouterAndAfterWorkbenchRebuild() {
        QObject window;
        QAction target(&window);
        DrnasDocumentOpenRouter router(&window, &target);
        QSignalSpy androidCalls(&target, &QAction::triggered);
        int nativeCalls = 0;
        for (int generation = 0; generation < 2; ++generation) {
            auto* owner = new QObject(&window);
            auto* action = new QAction(owner);
            action->setObjectName(QStringLiteral("Std_Open"));
            QObject::connect(action, &QAction::triggered, owner, [&] { ++nativeCalls; });
            QToolBar bar;
            bar.addAction(action);
            QCoreApplication::processEvents();
            action->trigger();
            QCOMPARE(nativeCalls, 0);
            QCOMPARE(androidCalls.count(), generation + 1);
            delete owner;
        }
    }
    void routeFirstInputBeforeDeferredCallback() {
        QObject window;
        QAction target(&window);
        DrnasDocumentOpenRouter router(&window, &target);
        QObject owner(&window);
        QAction action(&owner);
        action.setObjectName(QStringLiteral("Std_Open"));
        int nativeCalls = 0;
        QObject::connect(&action, &QAction::triggered, &owner, [&] { ++nativeCalls; });
        QSignalSpy androidCalls(&target, &QAction::triggered);
        // Deliberately don't process the queued ChildAdded callback first.
        QEvent press(QEvent::MouseButtonPress);
        QCoreApplication::sendEvent(&window, &press);
        action.trigger();
        QCOMPARE(nativeCalls, 0);
        QCOMPARE(androidCalls.count(), 1);
    }
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
