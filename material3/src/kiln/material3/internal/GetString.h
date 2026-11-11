#pragma once

// getString (material3/internal/Strings.android.kt): material3's strings in the configuration's
// locales, formatted like Resources.getString(id, args).

#include "kiln/material3/internal/Strings.h"
#include "kiln/text/LocaleFormat.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace kiln::material3 {

    // Resources resolution over a locale list (API 24+): the first locale that has a value of
    // its own for the id (its tag, then its fallbacks), else the default value.
    std::string_view ResolveString(Strings id, const std::vector<std::string>& locales);

    // getString(string): reads LocalConfiguration (so callers recompose when locales change).
    std::string GetString(Strings id);
    // getString(string, formatArgs): String.format in LocalConfiguration's first locale.
    std::string GetString(Strings id, std::initializer_list<text::FormatArg> formatArgs);
    // The same outside composition (semantics, drawing), with the locales a composition read
    // (LocalConfiguration's, via CurrentLocales()).
    std::string GetString(Strings id, const std::vector<std::string>& locales);
    std::string GetString(Strings id, const std::vector<std::string>& locales, std::initializer_list<text::FormatArg> formatArgs);
    // LocalConfiguration's locales in the current composition (none outside one).
    std::vector<std::string> CurrentLocales();

}
