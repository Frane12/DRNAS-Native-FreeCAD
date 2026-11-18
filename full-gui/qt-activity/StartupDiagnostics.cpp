#include "StartupDiagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <cstdlib>
#include <QtGlobal>
#ifdef Q_OS_ANDROID
#include <android/log.h>
#include <QJniEnvironment>
#include <QJniObject>
#include <QCoreApplication>
#include "DrnasCrashFrames.h"
#endif

namespace {
QMutex logMutex;
QString currentLog;
QString previousLog;
QtMessageHandler previousQtHandler = nullptr;
constexpr qint64 maxLogSize = 4 * 1024 * 1024;

void appendLocked(const QString& message)
{
    if (currentLog.isEmpty()) return;
    QFile log(currentLog);
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    if (log.size() > maxLogSize) {
        log.close();
        if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        log.write("[DRNAS] Previous log lines rotated after 4 MiB.\n");
    }
    const QByteArray line = (QDateTime::currentDateTimeUtc().toString(
        Qt::ISODateWithMs) + QStringLiteral(" | ") + message
        + QLatin1Char('\n')).toUtf8();
    log.write(line);
    log.flush(); // Persist even if a subsequent native dlopen aborts.
}

QString tailOf(const QString& path, qint64 limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("(no log yet)\n");
    if (file.size() > limit) file.seek(file.size() - limit);
    return QString::fromUtf8(file.readAll());
}

void qtLogToFile(QtMsgType type, const QMessageLogContext& context,
                 const QString& message)
{
    const char* severity = "DEBUG";
    switch (type) {
    case QtInfoMsg: severity = "INFO"; break;
    case QtWarningMsg: severity = "WARN"; break;
    case QtCriticalMsg: severity = "ERROR"; break;
    case QtFatalMsg: severity = "FATAL"; break;
    case QtDebugMsg: break;
    }
    const QString category = QString::fromLatin1(
        context.category ? context.category : "qt");
    {
        QMutexLocker lock(&logMutex);
        appendLocked(QStringLiteral("[%1] [%2] %3")
            .arg(QString::fromLatin1(severity), category, message));
    }
    // Do not suppress regular Android logcat/other existing Qt handlers.
    if (previousQtHandler) {
        previousQtHandler(type, context, message);
    }
#ifdef Q_OS_ANDROID
    else {
        int priority = ANDROID_LOG_DEBUG;
        if (type == QtInfoMsg) priority = ANDROID_LOG_INFO;
        else if (type == QtWarningMsg) priority = ANDROID_LOG_WARN;
        else if (type == QtCriticalMsg || type == QtFatalMsg)
            priority = ANDROID_LOG_ERROR;
        __android_log_write(priority, "DRNAS-FreeCAD", message.toUtf8().constData());
    }
#endif
}
} // namespace

void DrnasDiagnostics::start()
{
    if (!currentLog.isEmpty()) return;
    const QString path = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + QStringLiteral("/diagnostics");
    if (!QDir().mkpath(path)) return;

    previousLog = path + QStringLiteral("/previous-launch.log");
    const QString next = path + QStringLiteral("/current-launch.log");
    QFile::remove(previousLog);
    if (QFile::exists(next)) {
        QFile prior(next);
        prior.rename(previousLog);
    }
    currentLog = next;
    // Shared app-private phase journal written directly by libFreeCADGui.so.
    // The native library cannot call this executable's Qt log handler reliably.
    const QByteArray nativePath = next.toUtf8();
    if (setenv("DRNAS_WB_LOG_PATH", nativePath.constData(), 1) != 0)
        note(QStringLiteral("WARN: native workbench log path unavailable"));
    QSettings state;
    const bool previousUnclean = state.value(
        QStringLiteral("drnasLog/sessionActive"), false).toBool();
    state.setValue(QStringLiteral("drnasLog/sessionActive"), true);
    state.sync();

    note(QStringLiteral("=== APPLICATION STARTED / QtActivity ==="));
    note(QStringLiteral("Platform=%1, architecture=%2, Qt=%3")
        .arg(QSysInfo::prettyProductName(),
             QSysInfo::currentCpuArchitecture(),
             QString::fromLatin1(qVersion())));
    if (previousUnclean)
        note(QStringLiteral("PREVIOUS SESSION DID NOT EXIT CLEANLY; see previous-launch.log"));

    previousQtHandler = qInstallMessageHandler(qtLogToFile);
    QObject::connect(QCoreApplication::instance(),
                     &QCoreApplication::aboutToQuit,
                     QCoreApplication::instance(), [] {
        DrnasDiagnostics::note(QStringLiteral("=== NORMAL APPLICATION EXIT ==="));
        QSettings session;
        session.setValue(QStringLiteral("drnasLog/sessionActive"), false);
        session.sync();
    });
    qInfo().noquote() << "DRNAS startup logging enabled in private app directory";
}

void DrnasDiagnostics::note(const QString& message)
{
    QMutexLocker lock(&logMutex);
    appendLocked(QStringLiteral("[STAGE] ") + message);
}

#ifdef Q_OS_ANDROID
// Retrieve the previous native crash from Android on explicit log export.
static QString lastNativeCrash() {
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 31) return {};
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const auto service = context.callObjectMethod("getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;", QJniObject::fromString("activity").object());
    if (!service.isValid()) return {};
    const auto package = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
    const auto exits = service.callObjectMethod("getHistoricalProcessExitReasons",
        "(Ljava/lang/String;II)Ljava/util/List;", package.object(), jint(0), jint(12));
    if (!exits.isValid()) return {};
    const int count = exits.callMethod<jint>("size");
    for (int i = 0; i < count; ++i) {
        const auto exit = exits.callObjectMethod("get", "(I)Ljava/lang/Object;", jint(i));
        if (!exit.isValid() || exit.callMethod<jint>("getReason") != 5) continue; // REASON_CRASH_NATIVE
        const auto trace = exit.callObjectMethod("getTraceInputStream", "()Ljava/io/InputStream;");
        if (!trace.isValid()) continue;
        QJniEnvironment env;
        constexpr jint chunk = 32768;
        constexpr int limit = 8 * 1024 * 1024;
        jbyteArray buffer = env->NewByteArray(chunk);
        if (env->ExceptionCheck()) env->ExceptionClear();
        QByteArray data;
        if (buffer) {
            while (data.size() <= limit) {
                const int n = trace.callMethod<jint>("read", "([B)I", buffer);
                if (n <= 0 || n > chunk) break;
                const auto offset = data.size();
                data.resize(offset + n);
                env->GetByteArrayRegion(buffer, 0, n, reinterpret_cast<jbyte*>(data.data() + offset));
                if (env->ExceptionCheck()) { env->ExceptionClear(); data.clear(); break; }
            }
            env->DeleteLocalRef(buffer);
        }
        trace.callMethod<void>("close");
        if (data.size() > limit) continue;
        const auto frames = DrnasCrashFrames::decode(data);
        if (frames.isEmpty()) continue;
        const auto stamp = QDateTime::fromMSecsSinceEpoch(exit.callMethod<jlong>("getTimestamp"), QTimeZone::UTC);
        return QStringLiteral("\nANDROID NATIVE CRASH %1\n%2\n")
            .arg(stamp.toString(Qt::ISODateWithMs), frames.join('\n'));
    }
    return {};
}
#endif

QString DrnasDiagnostics::report()
{
#ifdef Q_OS_ANDROID
    // JNI can log while reading the previous exit. Keep it outside logMutex.
    const QString crash = lastNativeCrash();
#else
    const QString crash;
#endif
    QMutexLocker lock(&logMutex);
    return QStringLiteral("DRNAS FreeCAD - diagnostic report\n"
                          "PREVIOUS LAUNCH (last 256 KiB)\n")
        + tailOf(previousLog, 256 * 1024)
        + QStringLiteral("\nCURRENT LAUNCH (last 256 KiB)\n")
        + tailOf(currentLog, 256 * 1024) + crash;
}
