#include "kiln/Log.h"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace kiln::log {

    namespace {
        std::atomic<Sink> g_Sink{ nullptr };
        std::atomic<LoggableCheck> g_Check{ nullptr };
        std::mutex g_StderrMutex;
        char LevelChar(Level l) {
            switch (l) {
                case Level::Verbose: return 'V';
                case Level::Debug: return 'D';
                case Level::Info: return 'I';
                case Level::Warn: return 'W';
                default: return 'E';
            }
        }
    }

    void SetSink(Sink sink) { g_Sink = sink; }
    void SetLoggableCheck(LoggableCheck check) { g_Check = check; }

    bool IsLoggable(Level level, std::string_view tag) {
        const LoggableCheck c = g_Check;
        return c ? c(level, tag) : true;
    }

    void Write(Level level, std::string_view tag, std::string_view message) {
        if (const Sink s = g_Sink) { s(level, tag, message); return; }
        std::lock_guard lock(g_StderrMutex);   // whole lines from concurrent threads
        std::fprintf(stderr, "%c/%.*s: %.*s\n", LevelChar(level), int(tag.size()), tag.data(), int(message.size()), message.data());
    }

}
