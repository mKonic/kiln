#include "Check.h"

#include "kiln/material3/internal/GetString.h"
#include "kiln/text/LocaleFormat.h"
#include "kiln/ui/Accessibility.h"

#include <span>

using namespace kiln;
using namespace kiln::text;
using kiln::material3::ResolveString;
using kiln::material3::Strings;

namespace {
    std::string Format(std::string_view pattern, std::vector<FormatArg> args, std::string_view locale) {
        return FormatJava(pattern, std::span<const FormatArg>(args), locale);
    }
}

TEST(ResourceCandidatesFollowAndroidFallback) {
    CHECK((ResourceLocaleCandidates("de-AT") == std::vector<std::string>{ "de-AT", "de", "" }));
    CHECK((ResourceLocaleCandidates("sr-Latn-RS") == std::vector<std::string>{ "sr-Latn-RS", "sr-Latn", "sr-RS", "sr", "" }));
    CHECK((ResourceLocaleCandidates("zh-Hant") == std::vector<std::string>{ "zh-Hant", "zh-TW", "zh", "" }));
    CHECK((ResourceLocaleCandidates("en-US-u-nu-arab") == std::vector<std::string>{ "en-US", "en", "" }));
    CHECK((ResourceLocaleCandidates("") == std::vector<std::string>{ "" }));
}

TEST(FormatJavaUsesPositionsAndTheLocalesDigits) {
    CHECK(Format("%1$d o'clock", { std::int64_t(7) }, "en-US") == "7 o'clock");
    CHECK(Format("%1$s - %2$s", { std::string("1990"), std::string("2030") }, "de") == "1990 - 2030");
    CHECK(Format("%2$s, %1$s", { std::string("a"), std::string("b") }, "") == "b, a");
    CHECK(Format("%s and %d%%", { std::string("x"), std::int64_t(5) }, "") == "x and 5%");
    // CLDR default numbering: ar-EG uses Arabic-Indic digits, plain ar uses 0-9, fa extended ones.
    CHECK(Format("%1$d", { std::int64_t(42) }, "ar-EG") == "٤٢");
    CHECK(Format("%1$d", { std::int64_t(42) }, "ar") == "42");
    CHECK(Format("%1$d", { std::int64_t(-3) }, "fa-IR") == "-۳");
    CHECK(LocaleZeroDigit("bn-IN") == U'০');   // inherited from bn
}

TEST(Material3StringsResolveOverTheLocaleList) {
    CHECK(ResolveString(Strings::Dialog, {}) == "Dialog");
    CHECK(ResolveString(Strings::Dialog, { "de-DE" }) == "Dialogfeld");
    CHECK(ResolveString(Strings::Dialog, { "en-GB" }) == "Dialogue");
    CHECK(ResolveString(Strings::Dialog, { "zh-Hant" }) == "對話方塊");
    CHECK(ResolveString(Strings::Dialog, { "sr-Latn-RS" }) == "Dijalog");
    // Hebrew from the iw resources, as Locale.toLanguageTag names it.
    CHECK(ResolveString(Strings::SnackbarDismiss, { "he-IL" }) == "סגירה");
    // A locale without resources gives way to the next one in the list, before the default.
    CHECK(ResolveString(Strings::Dialog, { "xx-YY", "de" }) == "Dialogfeld");
}

TEST(AccessibilityStringsReadHebrewAndIndonesian) {
    CHECK(ui::a11y::Strings::ForLocale("he-IL").tab == "כרטיסייה");
    CHECK(ui::a11y::Strings::ForLocale("id").stateOn == "Aktif");
}
