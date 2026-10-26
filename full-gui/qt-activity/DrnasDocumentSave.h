#pragma once

#include "DrnasDocumentOpen.h"
#include "DrnasSaveNativeTrace.h"
#include "StartupDiagnostics.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QWindow>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

class DrnasDocumentSave final : public QObject {
    QPointer<QMainWindow> shell_;
    QPointer<QAction> openSelect_;
    QPointer<QAction> openAction_;
    std::function<bool(const QString&, QString&)> python_;
    std::function<void(const QString&)> report_;
    QTimer poll_;
    QJsonObject snapshot_;
    bool busy_ = false;
    bool copyOnly_ = false;

    static QString pythonText(const QString& text) {
        return QStringLiteral("__import__('base64').b64decode('%1').decode('utf-8')")
            .arg(QString::fromLatin1(text.toUtf8().toBase64()));
    }

    void release() {
        poll_.stop();
        busy_ = false;
        if (openSelect_) openSelect_->setEnabled(true);
        if (openAction_) openAction_->setEnabled(true);
    }

    void finish(const QJsonObject& result) {
        const QString status = result.value(QStringLiteral("status")).toString();
        const QString uri = result.value(QStringLiteral("uri")).toString();
        const bool success = status == QStringLiteral("SUCCESS") && uri.startsWith(QStringLiteral("content://"));
        const QString key = QStringLiteral("androidSave/destination/") + snapshot_.value(QStringLiteral("key")).toString();
        QSettings settings;
        if (success && !copyOnly_) settings.setValue(key, uri);
        if (!success && status != QStringLiteral("CANCELLED")) settings.remove(key);
        settings.setValue(QStringLiteral("androidSave/lastStatus"), success ? QStringLiteral("SUCCESS") : status);
        settings.sync();
        QString error;
        const bool acknowledged = python_(QStringLiteral(
            "from drnas_android_save import finish_save\n"
            "finish_save(%1, %2, %3)\n")
            .arg(pythonText(snapshot_.value(QStringLiteral("token")).toString()),
                 success ? QStringLiteral("True") : QStringLiteral("False"),
                 copyOnly_ ? QStringLiteral("False") : QStringLiteral("True")), error);
        DrnasDiagnostics::note(QStringLiteral("ANDROID_SAVE PROVIDER_RETURN status=%1 acknowledged=%2")
                              .arg(success ? QStringLiteral("SUCCESS") : status).arg(acknowledged));
        release();
        if (success) {
            report_(acknowledged ? QStringLiteral("FCStd spremljen na odabrano odredište")
                                 : QStringLiteral("FCStd spremljen; status dokumenta nije osvježen"));
        } else if (status == QStringLiteral("CANCELLED")) {
            report_(QStringLiteral("Spremanje otkazano; lokalna kopija je sačuvana"));
        } else {
            report_(QStringLiteral("Spremanje nije uspjelo; lokalna kopija je sačuvana. ")
                    + result.value(QStringLiteral("message")).toString().left(180));
        }
    }

    void start(bool saveAs, bool copyOnly) {
        if (busy_) return;
        if (!openSelect_ || !openSelect_->isEnabled()) {
            report_(QStringLiteral("Pričekaj završetak odabira datoteke"));
            return;
        }
        busy_ = true;
        copyOnly_ = copyOnly;
        openSelect_->setEnabled(false);
        if (openAction_) openAction_->setEnabled(false);
        const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/drnas-documents");
        if (!QDir().mkpath(root)) {
            release();
            report_(QStringLiteral("Nije moguće pripremiti mapu za spremanje"));
            return;
        }
        const QString request = root + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces);
        DrnasDiagnostics::note(QStringLiteral("ANDROID_SAVE REQUEST saveAs=%1 copyOnly=%2").arg(saveAs).arg(copyOnly));
        report_(QStringLiteral("Priprema dokumenta za spremanje…"));
        QString error;
        bool serialized = false;
        {
            DrnasSaveNativeTrace nativeTrace;
            serialized = python_(QStringLiteral("from drnas_android_save import prepare_save\nprepare_save(%1)\n")
                                 .arg(pythonText(request)), error);
        }
        if (!serialized) {
            release();
            DrnasDiagnostics::note(QStringLiteral("ANDROID_SAVE SERIALIZE_FAILED ") + error.left(250));
            report_(QStringLiteral("Priprema FCStd-a nije uspjela: ") + error.left(180));
            return;
        }
        QFile metadata(request + QStringLiteral("/snapshot.json"));
        if (!metadata.open(QIODevice::ReadOnly)) {
            release();
            report_(QStringLiteral("Nedostaje potvrda pripremljenog FCStd-a"));
            return;
        }
        snapshot_ = QJsonDocument::fromJson(metadata.readAll()).object();
        const QString path = snapshot_.value(QStringLiteral("path")).toString();
        if (snapshot_.value(QStringLiteral("token")).toString().isEmpty() || !QFile::exists(path)) {
            release();
            report_(QStringLiteral("Pripremljeni FCStd nije dostupan"));
            return;
        }
        QSettings settings;
        settings.setValue(QStringLiteral("androidSave/lastLocalCopy"), path);
        settings.sync();
        const QString destination = saveAs ? QString() : settings.value(QStringLiteral("androidSave/destination/")
            + snapshot_.value(QStringLiteral("key")).toString()).toString();
#ifdef Q_OS_ANDROID
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        const QJniObject source = QJniObject::fromString(path);
        const QJniObject filename = QJniObject::fromString(snapshot_.value(QStringLiteral("filename")).toString());
        const QJniObject target = QJniObject::fromString(destination);
        const bool launched = activity.isValid() && QJniObject::callStaticMethod<jboolean>(
            "org/drnas/freecad/bootstrap/AndroidSaveBridge", "launchSave",
            "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
            activity.object<jobject>(), source.object<jstring>(), filename.object<jstring>(), target.object<jstring>());
        if (!launched) {
            finish(QJsonObject{{QStringLiteral("status"), QStringLiteral("ERROR")},
                               {QStringLiteral("message"), QStringLiteral("Android Save nije pokrenut")}});
            return;
        }
        report_(destination.isEmpty() ? QStringLiteral("Odaberi ime i mapu za FCStd")
                                      : QStringLiteral("Spremanje FCStd dokumenta…"));
        poll_.start(300);
#else
        finish(QJsonObject{{QStringLiteral("status"), QStringLiteral("ERROR")},
                           {QStringLiteral("message"), QStringLiteral("Android Save dostupan je na Androidu")}});
#endif
    }
public:
    DrnasDocumentSave(QMainWindow* shell, QMainWindow* cad, QAction* openSelect, QAction* openAction,
                     std::function<bool(const QString&, QString&)> python,
                     std::function<void(const QString&)> report)
        : QObject(shell), shell_(shell), openSelect_(openSelect), openAction_(openAction),
          python_(std::move(python)), report_(std::move(report)), poll_(this) {
        struct Command { const char* name; bool saveAs; bool copyOnly; };
        for (const auto command : {Command{"Std_Save", false, false},
                                   Command{"Std_SaveAs", true, false},
                                   Command{"Std_SaveCopy", true, true}}) {
            auto* target = new QAction(this);
            QObject::connect(target, &QAction::triggered, this, [this, command] { start(command.saveAs, command.copyOnly); });
            new DrnasDocumentActionRouter(cad, target, [command](int count) {
                DrnasDiagnostics::note(QStringLiteral("ANDROID_SAVE %1 ROUTED count=%2")
                                      .arg(QString::fromLatin1(command.name)).arg(count));
            }, QString::fromLatin1(command.name));
        }
        QObject::connect(&poll_, &QTimer::timeout, this, [this] {
#ifdef Q_OS_ANDROID
            if (!shell_ || QGuiApplication::applicationState() != Qt::ApplicationActive ||
                !shell_->windowHandle() || !shell_->windowHandle()->isExposed()) return;
            const QJniObject result = QJniObject::callStaticObjectMethod(
                "org/drnas/freecad/bootstrap/AndroidSaveBridge", "pollSaveResult", "()Ljava/lang/String;");
            const QString value = result.isValid() ? result.toString() : QStringLiteral("{}");
            if (value == QStringLiteral("PENDING") || value == QStringLiteral("IDLE")) return;
            finish(QJsonDocument::fromJson(value.toUtf8()).object());
#endif
        });
    }
};
