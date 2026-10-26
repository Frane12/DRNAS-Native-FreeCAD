#include "../DrnasDocumentOpen.h"
#include "../DrnasDocumentSave.h"
#include <QApplication>
#include <QMenu>
#include <QToolBar>
#include <QtTest>

class DocumentOpenTests : public QObject {
    Q_OBJECT
private slots:
    void routeLateSaveAndSaveAsSeparatelyFromOpen() {
        QObject window;
        QAction saveTarget(&window), asTarget(&window), open(&window);
        DrnasDocumentActionRouter saveRouter(&window, &saveTarget, {}, QStringLiteral("Std_Save"));
        DrnasDocumentActionRouter asRouter(&window, &asTarget, {}, QStringLiteral("Std_SaveAs"));
        QObject owner(&window);
        QAction save(&owner), saveAs(&owner);
        save.setObjectName(QStringLiteral("Std_Save"));
        saveAs.setObjectName(QStringLiteral("Std_SaveAs"));
        open.setObjectName(QStringLiteral("Std_Open"));
        int oldCalls = 0;
        QObject::connect(&save, &QAction::triggered, &owner, [&] { ++oldCalls; });
        QObject::connect(&saveAs, &QAction::triggered, &owner, [&] { ++oldCalls; });
        QSignalSpy saved(&saveTarget, &QAction::triggered), savedAs(&asTarget, &QAction::triggered);
        QCoreApplication::processEvents();
        save.trigger();
        QCOMPARE(saved.count(), 1);
        QCOMPARE(savedAs.count(), 0);
        saveAs.trigger();
        QCOMPARE(savedAs.count(), 1);
        QCOMPARE(oldCalls, 0);
        QVERIFY(!open.property("drnasAndroidActionRouted").toBool());
    }
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
