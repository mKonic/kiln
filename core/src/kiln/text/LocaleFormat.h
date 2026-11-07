#pragma once

// Locale pieces for platform strings: which resource tables a locale reads (Android's resource
// fallback), its digits, and java.util.Formatter's positional %s / %d, as Resources.getString
// (id, args) formats them.

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

    using FormatArg = std::variant<std::string, std::int64_t>;

    // String.format(locale, pattern, args) for the conversions platform strings use: %s, %d,
    // their positional forms (%1$s) and %%. Integers use the locale's digits.
    std::string FormatJava(std::string_view pattern, std::span<const FormatArg> args, std::string_view localeTag);

}
