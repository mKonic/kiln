#pragma once

// Typography (material3 Typography.kt): the baseline and emphasized type scale.

#include "kiln/material3/tokens/TokenTypes.h"

#include <optional>

namespace kiln::material3 {

    struct Typography {
        text::TextStyle displayLarge;
        text::TextStyle displayMedium;
        text::TextStyle displaySmall;
        text::TextStyle headlineLarge;
        text::TextStyle headlineMedium;
        text::TextStyle headlineSmall;
        text::TextStyle titleLarge;
        text::TextStyle titleMedium;
        text::TextStyle titleSmall;
        text::TextStyle bodyLarge;
        text::TextStyle bodyMedium;
        text::TextStyle bodySmall;
        text::TextStyle labelLarge;
        text::TextStyle labelMedium;
        text::TextStyle labelSmall;
        text::TextStyle displayLargeEmphasized;
        text::TextStyle displayMediumEmphasized;
        text::TextStyle displaySmallEmphasized;
        text::TextStyle headlineLargeEmphasized;
        text::TextStyle headlineMediumEmphasized;
        text::TextStyle headlineSmallEmphasized;
        text::TextStyle titleLargeEmphasized;
        text::TextStyle titleMediumEmphasized;
        text::TextStyle titleSmallEmphasized;
        text::TextStyle bodyLargeEmphasized;
        text::TextStyle bodyMediumEmphasized;
        text::TextStyle bodySmallEmphasized;
        text::TextStyle labelLargeEmphasized;
        text::TextStyle labelMediumEmphasized;
        text::TextStyle labelSmallEmphasized;

        // Typography(): the type scale tokens, each emphasized style the same as its plain one
        // (Kotlin resolves Compose's Typography() to the constructor without emphasized styles).
        Typography();
        // Typography(fontFamily, ...): the tokens' styles in `fontFamily`; a style given in
        // `overrides` replaces the token one, taking `fontFamily` when it names no family.
        struct Overrides {
            std::optional<text::TextStyle> displayLarge;
            std::optional<text::TextStyle> displayMedium;
            std::optional<text::TextStyle> displaySmall;
            std::optional<text::TextStyle> headlineLarge;
            std::optional<text::TextStyle> headlineMedium;
            std::optional<text::TextStyle> headlineSmall;
            std::optional<text::TextStyle> titleLarge;
            std::optional<text::TextStyle> titleMedium;
            std::optional<text::TextStyle> titleSmall;
            std::optional<text::TextStyle> bodyLarge;
            std::optional<text::TextStyle> bodyMedium;
            std::optional<text::TextStyle> bodySmall;
            std::optional<text::TextStyle> labelLarge;
            std::optional<text::TextStyle> labelMedium;
            std::optional<text::TextStyle> labelSmall;
            std::optional<text::TextStyle> displayLargeEmphasized;
            std::optional<text::TextStyle> displayMediumEmphasized;
            std::optional<text::TextStyle> displaySmallEmphasized;
            std::optional<text::TextStyle> headlineLargeEmphasized;
            std::optional<text::TextStyle> headlineMediumEmphasized;
            std::optional<text::TextStyle> headlineSmallEmphasized;
            std::optional<text::TextStyle> titleLargeEmphasized;
            std::optional<text::TextStyle> titleMediumEmphasized;
            std::optional<text::TextStyle> titleSmallEmphasized;
            std::optional<text::TextStyle> bodyLargeEmphasized;
            std::optional<text::TextStyle> bodyMediumEmphasized;
            std::optional<text::TextStyle> bodySmallEmphasized;
            std::optional<text::TextStyle> labelLargeEmphasized;
            std::optional<text::TextStyle> labelMediumEmphasized;
            std::optional<text::TextStyle> labelSmallEmphasized;
        };
        explicit Typography(const text::FontFamily& fontFamily, const Overrides& overrides = {});

        bool operator==(const Typography&) const = default;
    };

    // Typography.fromToken (TypographyKeyTokens id to style; bodyLarge for an unknown id).
    const text::TextStyle& FromToken(const Typography& typography, tokens::TypographyToken token);

}
