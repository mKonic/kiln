#pragma once

// Logging for apps: logcat on Android (android.util.Log's priorities, the tag's loggability
// respected), stderr elsewhere.
//
//   kiln::log::Info("Notes", "loaded {} notes in {} ms", count, ms);
//   kiln::log::Error("Notes", "cannot open the database: {}", e.what());

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace kiln::log {

    enum class Level : int { Verbose = 2, Debug = 3, Info = 4, Warn = 5, Error = 6 };

    // Where messages go (the host installs logcat). Null: stderr.
    using Sink = void (*)(Level level, std::string_view tag, std::string_view message);
    void SetSink(Sink sink);
    // Log.isLoggable: whether a message of this level and tag would be kept (on Android
    // `setprop log.tag.<tag> D` changes it; Info and above by default). Everything elsewhere.
    using LoggableCheck = bool (*)(Level level, std::string_view tag);
    void SetLoggableCheck(LoggableCheck check);
    bool IsLoggable(Level level, std::string_view tag);

    void Write(Level level, std::string_view tag, std::string_view message);

    // Formatted with std::format only when the message would be kept.
    template<typename... A>
    void Print(Level level, std::string_view tag, std::format_string<A...> fmt, A&&... args) {
        if (!IsLoggable(level, tag)) return;
        Write(level, tag, std::format(fmt, std::forward<A>(args)...));
    }
    template<typename... A> void Verbose(std::string_view tag, std::format_string<A...> fmt, A&&... a) { Print(Level::Verbose, tag, fmt, std::forward<A>(a)...); }
    template<typename... A> void Debug(std::string_view tag, std::format_string<A...> fmt, A&&... a) { Print(Level::Debug, tag, fmt, std::forward<A>(a)...); }
    template<typename... A> void Info(std::string_view tag, std::format_string<A...> fmt, A&&... a) { Print(Level::Info, tag, fmt, std::forward<A>(a)...); }
    template<typename... A> void Warn(std::string_view tag, std::format_string<A...> fmt, A&&... a) { Print(Level::Warn, tag, fmt, std::forward<A>(a)...); }
    template<typename... A> void Error(std::string_view tag, std::format_string<A...> fmt, A&&... a) { Print(Level::Error, tag, fmt, std::forward<A>(a)...); }

}
