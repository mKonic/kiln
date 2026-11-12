#pragma once

// Case mapping (ui-text String.kt, AnnotatedString.kt toUpperCase / toLowerCase / capitalize /
// decapitalize): locale-aware through the host (String.uppercase(locale) and friends on
// Android), else a simple built-in mapping. For an AnnotatedString each run between annotation
// boundaries maps on its own, and the annotations move to the mapped offsets (a mapping can
// change the length: "ß" upper-cases to "SS").

#include "kiln/text/AnnotatedString.h"

#include <functional>
#include <string>
#include <string_view>

namespace kiln::text {

    enum class CaseOp : std::uint8_t { Upper, Lower, Capitalize, Decapitalize };
    using CaseMapper = std::function<std::u16string(std::u16string_view text, const std::string& localeTag, CaseOp op)>;
    // The host's mapping (an empty locale tag: the default locale).
    void SetCaseMapper(CaseMapper mapper);
    std::u16string MapCase(std::u16string_view text, const std::string& localeTag, CaseOp op);

    // UTF-8 strings.
    std::string ToUpperCase(std::string_view text, const std::string& localeTag = {});
    std::string ToLowerCase(std::string_view text, const std::string& localeTag = {});
    // capitalize: the first character in title case when it is lower case; decapitalize: it in lower case.
    std::string Capitalize(std::string_view text, const std::string& localeTag = {});
    std::string Decapitalize(std::string_view text, const std::string& localeTag = {});

    AnnotatedString ToUpperCase(const AnnotatedString& text, const std::string& localeTag = {});
    AnnotatedString ToLowerCase(const AnnotatedString& text, const std::string& localeTag = {});
    AnnotatedString Capitalize(const AnnotatedString& text, const std::string& localeTag = {});
    AnnotatedString Decapitalize(const AnnotatedString& text, const std::string& localeTag = {});

}
