// Android investigation only: native GLES errors, without extra glGetError reads.
#pragma once
#ifdef __ANDROID__
#include <QOpenGLDebugLogger>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QHash>
#include <QVariant>
#include <Base/AndroidDocumentDiagnostics.h>
#include <dlfcn.h>
#include <unwind.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
struct DrnasGlesStack {
    char text[4096] = {};
    size_t used = 0;
    unsigned int frames = 0;
};

static _Unwind_Reason_Code drnasGlesStackFrame(_Unwind_Context* frame, void* opaque)
{
    auto* stack = static_cast<DrnasGlesStack*>(opaque);
    const uintptr_t pc = _Unwind_GetIP(frame);
    if (!pc || stack->frames++ >= 24 || stack->used >= sizeof(stack->text)-1)
        return _URC_END_OF_STACK;
    Dl_info info = {};
    if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
        const char* base = std::strrchr(info.dli_fname, '/');
        const int n = std::snprintf(stack->text + stack->used,
            sizeof(stack->text) - stack->used, " %s+0x%lx(%s)",
            base ? base + 1 : info.dli_fname,
            static_cast<unsigned long>(pc - reinterpret_cast<uintptr_t>(info.dli_fbase)),
            info.dli_sname ? info.dli_sname : "?");
        if (n > 0) {
            const size_t remaining = sizeof(stack->text) - stack->used - 1;
            stack->used += size_t(n) < remaining ? size_t(n) : remaining;
        }
    }
    return _URC_NO_REASON;
}

// Inspect the loaded Coin DSO without loading another copy or touching GL.
static void drnasCoinProvenanceProbe()
{
    static bool once = false;
    if (once) return;
    once = true;
    void* coin = dlopen("libCoin.so", RTLD_NOW | RTLD_NOLOAD);
    void* symbol = coin ? dlsym(coin, "drnas_coin_android_gles_guard_v2")
                        : dlsym(RTLD_DEFAULT, "drnas_coin_android_gles_guard_v2");
    Dl_info info = {};
    if (symbol) dladdr(symbol, &info);
    using VersionFn = const char* (*)();
    const char* version = symbol ? reinterpret_cast<VersionFn>(symbol)() : "MISSING";
    char line[1024];
    std::snprintf(line, sizeof(line), "COIN_NATIVE_PROVENANCE marker=%s loaded=%d path=%s",
                  version, int(coin != nullptr),
                  info.dli_fname ? info.dli_fname : "unknown");
    ::drnasDocumentPhase(line, symbol ? "PROVENANCE_PASS" : "PROVENANCE_MISSING");
    if (coin) dlclose(coin);
}

static void drnasInstallGlesDebug(QOpenGLWidget* widget)
{
    auto* context = QOpenGLContext::currentContext();
    if (!widget || !context || !context->isValid() || context != widget->context()) return;
    // Property belongs to the actual context, including after context recreation.
    if (context->property("drnasGlesDebugAttempted").toBool()) {
        const unsigned int frames = context->property("drnasGlesDebugFrames").toUInt() + 1;
        context->setProperty("drnasGlesDebugFrames", frames);
        if (frames % 240 == 0) {
            const QByteArray status = context->property("drnasGlesDebugStatus").toByteArray();
            ::drnasDocumentPhase(status.constData(), "LOGGER_STATUS");
        }
        return;
    }
    context->setProperty("drnasGlesDebugAttempted", true);
    auto* logger = new QOpenGLDebugLogger(context);
    if (!logger->initialize()) {
        context->setProperty("drnasGlesDebugStatus", QByteArray("GLES_DEBUG unsupported"));
        ::drnasDocumentPhase("GLES_DEBUG unsupported", "LOGGER_UNAVAILABLE");
        delete logger;
        return;
    }
    QObject::connect(logger, &QOpenGLDebugLogger::messageLogged, logger,
        [counts = QHash<QString, unsigned int>()](const QOpenGLDebugMessage& message) mutable {
            // Do not issue GL calls, change contexts or invoke GUI commands here.
            if (message.type() != QOpenGLDebugMessage::ErrorType) return;
            QString detail = message.message().left(768);
            detail.replace('\n', ' ');
            detail.replace('\r', ' ');
            const QString key = QStringLiteral("%1/%2/%3").arg(int(message.source()))
                .arg(message.id()).arg(detail);
            // Bound distinct signatures and per-signature writes, not just total errors.
            if (!counts.contains(key) && counts.size() >= 32) return;
            const unsigned int n = ++counts[key];
            if (n > 4 && n % 120 != 0) return;
            const QByteArray bytes = detail.toUtf8();
            char line[1200];
            std::snprintf(line, sizeof(line),
                "GLES_DEBUG source=%d type=%d severity=%d id=%u sample=%u message=%s",
                int(message.source()), int(message.type()), int(message.severity()),
                message.id(), n, bytes.constData());
            ::drnasDocumentPhase(line, "DRIVER_ERROR");
            // Synchronous callback: stack still contains the offending native call.
            DrnasGlesStack stack;
            _Unwind_Backtrace(drnasGlesStackFrame, &stack);
            ::drnasDocumentPhase(stack.text, "GLES_DEBUG_STACK");
        }, Qt::DirectConnection);
    logger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
    logger->enableMessages(QOpenGLDebugMessage::AnySource, QOpenGLDebugMessage::ErrorType,
                           QOpenGLDebugMessage::AnySeverity);
    char status[256];
    std::snprintf(status, sizeof(status),
        "GLES_DEBUG context=%p debugContext=%d logging=%d version=%d.%d",
        static_cast<void*>(context), int(context->format().testOption(QSurfaceFormat::DebugContext)),
        int(logger->isLogging()), context->format().majorVersion(), context->format().minorVersion());
    context->setProperty("drnasGlesDebugStatus", QByteArray(status));
    ::drnasDocumentPhase(status, "LOGGER_READY");
}
} // namespace
#endif
