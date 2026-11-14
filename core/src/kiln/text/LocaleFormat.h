#pragma once

// Locale pieces for platform strings: which resource tables a locale reads (Android's resource
// fallback), its digits, and java.util.Formatter's positional %s / %d, as Resources.getString
// (id, args) formats them.

#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kiln::text {

    // The resource tags to try for a BCP 47 locale tag, best first, ending with "" (the
    // default resources): the full tag, without the script, a script's usual region (zh-Hant to
    // zh-TW, zh-Hans to zh-CN), then the language.
    std::vector<std::string> ResourceLocaleCandidates(std::string_view tag);

    // The zero digit of the locale's default numbering system (CLDR; U'0' for most locales).
    char32_t LocaleZeroDigit(std::string_view tag);

    // A format argument: a string, an integer or a floating-point number.
    struct FormatArg {
        std::variant<std::string, std::int64_t, double> value;
        FormatArg(std::string s) : value(std::move(s)) {}
        FormatArg(const char* s) : value(std::string(s ? s : "null")) {}
        FormatArg(std::string_view s) : value(std::string(s)) {}
        template<std::integral T> FormatArg(T v) : value(std::int64_t(v)) {}
        template<std::floating_point T> FormatArg(T v) : value(double(v)) {}
    };

    // String.format(locale, pattern, args) (java.util.Formatter): %[index$][flags][width]
    // [.precision]conversion for s S b B c C d o x X e E f g G h H, %% and %n, with flags
    // '-', '#', '+', ' ', '0', ',' and '('. Numbers use the locale's digits, decimal separator
    // and grouping separator (a CLDR subset: Android formats app strings with the platform's
    // String.format instead).
    std::string FormatJava(std::string_view pattern, std::span<const FormatArg> args, std::string_view localeTag);
    // The locale's decimal and grouping separators (UTF-8).
    std::string_view LocaleDecimalSeparator(std::string_view tag);
    std::string_view LocaleGroupingSeparator(std::string_view tag);

}
