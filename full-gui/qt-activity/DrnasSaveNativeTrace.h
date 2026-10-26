#pragma once

// Scoped C++ termination diagnostics for native document serialization. This
// does not catch signals, suppress failures, or call Qt from a fatal handler.
#if defined(Q_OS_ANDROID) || defined(DRNAS_SAVE_TRACE_HOST_TEST)
#include <cxxabi.h>
#include <dlfcn.h>
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unwind.h>
#include <unistd.h>

class DrnasSaveNativeTrace {
    inline static thread_local int journal_ = -1;
    inline static thread_local std::terminate_handler previous_ = nullptr;
    int oldJournal_ = -1;
    std::terminate_handler oldPrevious_ = nullptr;
    std::terminate_handler replaced_ = nullptr;

    static void record(const char* message) noexcept {
        std::fprintf(stderr, "[SAVE_NATIVE] %s\n", message);
        std::fflush(stderr);
        if (journal_ >= 0) {
            char line[1400];
            const int size = std::snprintf(line, sizeof(line), "[SAVE_NATIVE] %s\n", message);
            if (size > 0) {
                const auto length = static_cast<size_t>(size) < sizeof(line)
                    ? static_cast<size_t>(size) : sizeof(line) - 1;
                (void)::write(journal_, line, length);
                (void)::fsync(journal_);
            }
        }
    }
    static _Unwind_Reason_Code frame(_Unwind_Context* context, void* state) noexcept {
        auto* count = static_cast<int*>(state);
        if ((*count)++ >= 40) return _URC_END_OF_STACK;
        const auto address = _Unwind_GetIP(context);
        Dl_info info = {};
        char line[1100];
        if (address && ::dladdr(reinterpret_cast<void*>(address), &info)) {
            const auto offset = address - reinterpret_cast<_Unwind_Ptr>(info.dli_fbase);
            std::snprintf(line, sizeof(line), "FRAME %d module=%s offset=0x%llx symbol=%s",
                *count, info.dli_fname ? info.dli_fname : "?",
                static_cast<unsigned long long>(offset), info.dli_sname ? info.dli_sname : "?");
        } else {
            std::snprintf(line, sizeof(line), "FRAME %d pc=0x%llx", *count,
                          static_cast<unsigned long long>(address));
        }
        record(line);
        return _URC_NO_REASON;
    }
    [[noreturn]] static void terminated() noexcept {
        record("CXX_TERMINATE during native Save");
        const auto* type = abi::__cxa_current_exception_type();
        record(type ? type->name() : "no active C++ exception");
        if (const auto exception = std::current_exception()) {
            try { std::rethrow_exception(exception); }
            catch (const std::exception& error) { record(error.what()); }
            catch (...) { record("exception does not match std::exception"); }
        }
        int count = 0;
        _Unwind_Backtrace(frame, &count);
        // Preserve the existing termination behavior and Python fatal journal.
        if (previous_ && previous_ != terminated) previous_();
        std::abort();
    }
public:
    DrnasSaveNativeTrace() noexcept : oldJournal_(journal_), oldPrevious_(previous_) {
        const char* path = std::getenv("DRNAS_WB_LOG_PATH");
        journal_ = path && *path ? ::open(path, O_WRONLY | O_APPEND | O_CLOEXEC) : -1;
        replaced_ = std::set_terminate(terminated);
        previous_ = replaced_;
        record("serialization trace armed");
    }
    ~DrnasSaveNativeTrace() {
        record("serialization trace returned");
        std::set_terminate(replaced_);
        if (journal_ >= 0) ::close(journal_);
        journal_ = oldJournal_;
        previous_ = oldPrevious_;
    }
    DrnasSaveNativeTrace(const DrnasSaveNativeTrace&) = delete;
    DrnasSaveNativeTrace& operator=(const DrnasSaveNativeTrace&) = delete;
};
#else
class DrnasSaveNativeTrace {};
#endif
