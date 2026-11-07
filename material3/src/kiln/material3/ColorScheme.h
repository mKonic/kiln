#pragma once

// ColorScheme (material3 ColorScheme.kt): the colour roles, the light / dark / expressive
// defaults, contentColorFor, surfaceColorAtElevation and the token lookup.

#include "kiln/material3/tokens/TokenTypes.h"

#include <functional>

namespace kiln::material3 {

    using ui::Color;

    // Every role, in the order ColorScheme declares them.
#define KILN_M3_COLOR_ROLES(X) \
    X(primary) \
    X(onPrimary) \
    X(primaryContainer) \
    X(onPrimaryContainer) \
    X(inversePrimary) \
    X(secondary) \
    X(onSecondary) \
    X(secondaryContainer) \
    X(onSecondaryContainer) \
    X(tertiary) \
    X(onTertiary) \
    X(tertiaryContainer) \
    X(onTertiaryContainer) \
    X(background) \
    X(onBackground) \
    X(surface) \
    X(onSurface) \
    X(surfaceVariant) \
    X(onSurfaceVariant) \
    X(surfaceTint) \
    X(inverseSurface) \
    X(inverseOnSurface) \
    X(error) \
    X(onError) \
    X(errorContainer) \
    X(onErrorContainer) \
    X(outline) \
    X(outlineVariant) \
    X(scrim) \
    X(surfaceBright) \
    X(surfaceDim) \
    X(surfaceContainer) \
    X(surfaceContainerHigh) \
    X(surfaceContainerHighest) \
    X(surfaceContainerLow) \
    X(surfaceContainerLowest) \
    X(primaryFixed) \
    X(primaryFixedDim) \
    X(onPrimaryFixed) \
    X(onPrimaryFixedVariant) \
    X(secondaryFixed) \
    X(secondaryFixedDim) \
    X(onSecondaryFixed) \
    X(onSecondaryFixedVariant) \
    X(tertiaryFixed) \
    X(tertiaryFixedDim) \
    X(onTertiaryFixed) \
    X(onTertiaryFixedVariant)

    struct ColorScheme {
        Color primary;
        Color onPrimary;
        Color primaryContainer;
        Color onPrimaryContainer;
        Color inversePrimary;
        Color secondary;
        Color onSecondary;
        Color secondaryContainer;
        Color onSecondaryContainer;
        Color tertiary;
        Color onTertiary;
        Color tertiaryContainer;
        Color onTertiaryContainer;
        Color background;
        Color onBackground;
        Color surface;
        Color onSurface;
        Color surfaceVariant;
        Color onSurfaceVariant;
        Color surfaceTint;
        Color inverseSurface;
        Color inverseOnSurface;
        Color error;
        Color onError;
        Color errorContainer;
        Color onErrorContainer;
        Color outline;
        Color outlineVariant;
        Color scrim;
        Color surfaceBright;
        Color surfaceDim;
        Color surfaceContainer;
        Color surfaceContainerHigh;
        Color surfaceContainerHighest;
        Color surfaceContainerLow;
        Color surfaceContainerLowest;
        Color primaryFixed;
        Color primaryFixedDim;
        Color onPrimaryFixed;
        Color onPrimaryFixedVariant;
        Color secondaryFixed;
        Color secondaryFixedDim;
        Color onSecondaryFixed;
        Color onSecondaryFixedVariant;
        Color tertiaryFixed;
        Color tertiaryFixedDim;
        Color onTertiaryFixed;
        Color onTertiaryFixedVariant;

        bool operator==(const ColorScheme&) const = default;
    };

    // lightColorScheme / darkColorScheme: the baseline roles (ColorLightTokens /
    // ColorDarkTokens). `customize` sets roles as the named arguments do; surfaceTint follows
    // primary unless it sets surfaceTint too.
    ColorScheme LightColorScheme(const std::function<void(ColorScheme&)>& customize = {});
    ColorScheme DarkColorScheme(const std::function<void(ColorScheme&)>& customize = {});
    // expressiveLightColorScheme: the light scheme with tone 30 on-container roles.
    ColorScheme ExpressiveLightColorScheme();

    // ColorScheme.contentColorFor: the role drawn on `backgroundColor`, or Unspecified when it
    // is not one of the scheme's background roles.
    Color ContentColorFor(const ColorScheme& scheme, Color backgroundColor);
    // surfaceColorAtElevation: surfaceTint over surface, more opaque with elevation.
    Color SurfaceColorAtElevation(const ColorScheme& scheme, ui::Dp elevation);
    // ColorScheme.fromToken (ColorSchemeKeyTokens id to role).
    Color FromToken(const ColorScheme& scheme, tokens::ColorToken token);

    inline constexpr float DisabledAlpha = 0.38f;

}
