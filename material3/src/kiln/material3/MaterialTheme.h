#pragma once

// MaterialTheme (material3 MaterialTheme.kt): the colour scheme, typography, shapes and motion
// scheme components read, provided to a subtree; with LocalContentColor (ContentColor.kt),
// LocalTextStyle / ProvideTextStyle (Text.kt), LocalTonalElevationEnabled (ColorScheme.kt) and
// LocalMinimumInteractiveComponentSize (InteractiveComponentSize.kt).

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>
#include "kiln/material3/ColorScheme.h"
#include "kiln/material3/MotionScheme.h"
#include "kiln/material3/Shapes.h"
#include "kiln/material3/Typography.h"

namespace kiln::material3 {

    // MaterialTheme.Values
    struct MaterialThemeValues {
        ColorScheme colorScheme = LightColorScheme();
        Typography typography;
        Shapes shapes;
        MotionSchemePtr motionScheme = MotionScheme::Standard();
        bool operator==(const MaterialThemeValues& o) const {
            return colorScheme == o.colorScheme && typography == o.typography && shapes == o.shapes && motionScheme == o.motionScheme;
        }
    };

    // MaterialTheme.LocalMaterialTheme
    const rt::Local<MaterialThemeValues>& LocalMaterialTheme();

    // MaterialTheme.colorScheme / typography / shapes / motionScheme in the current composition.
    struct MaterialTheme {
        static const ColorScheme& colorScheme();
        static const Typography& typography();
        static const Shapes& shapes();
        static const MotionSchemePtr& motionScheme();
    };

    // MaterialTheme(colorScheme, motionScheme, shapes, typography, content): unset values keep
    // the enclosing theme's. Provides the theme, ripple() as the indication, text selection
    // colours from primary and bodyLarge as the text style.
    struct MaterialThemeArgs {
        std::optional<ColorScheme> colorScheme;
        std::optional<MotionSchemePtr> motionScheme;
        std::optional<Shapes> shapes;
        std::optional<Typography> typography;
    };
    void MaterialTheme(const MaterialThemeArgs& args, kiln::Function<void()> content);
    inline void MaterialTheme(kiln::Function<void()> content) { MaterialTheme({}, std::move(content)); }

    // MaterialExpressiveTheme: inside another expressive theme it behaves like MaterialTheme;
    // otherwise unset values take the expressive defaults (expressive light colours, the
    // expressive motion scheme, default shapes and typography).
    void MaterialExpressiveTheme(const MaterialThemeArgs& args, kiln::Function<void()> content);

    // LocalContentColor: the preferred colour for content (text, icons) at this point.
    inline rt::Local<Color> LocalContentColor{ Color(0xFF000000u) };
    // contentColorFor: the theme's role for `backgroundColor`, else LocalContentColor.
    Color ContentColorFor(Color backgroundColor);

    // LocalTextStyle and ProvideTextStyle (the given style merged over the current one).
    const rt::Local<text::TextStyle>& LocalTextStyle();
    void ProvideTextStyle(const text::TextStyle& value, kiln::Function<void()> content);

    // LocalTonalElevationEnabled, and ColorScheme.applyTonalElevation: surface at an elevation
    // takes the tonal overlay while enabled.
    inline rt::Local<bool> LocalTonalElevationEnabled{ true };
    Color ApplyTonalElevation(const ColorScheme& scheme, Color backgroundColor, ui::Dp elevation);

    // ColorToken.value / TypographyToken.value / ShapeToken.value / MotionSchemeToken.value:
    // token ids resolved against the current theme.
    Color Value(tokens::ColorToken token);
    const text::TextStyle& Value(tokens::TypographyToken token);
    foundation::ShapePtr Value(tokens::ShapeToken token);
    anim::SpecPtr Value(tokens::MotionSchemeToken token);

    // LocalMinimumInteractiveComponentSize (48 dp; 0 dp turns enforcement off) and
    // Modifier.minimumInteractiveComponentSize: at least that size, the content centred. Built
    // in composition, it reads the local there.
    inline rt::Local<ui::Dp> LocalMinimumInteractiveComponentSize{ ui::Dp(48) };
    ui::Modifier MinimumInteractiveComponentSize(ui::Modifier m);
    // MinimumInteractiveTopAlignmentLine / MinimumInteractiveLeftAlignmentLine: the content's
    // offset inside the enforced size (merged by minimum).
    inline constexpr ui::AlignmentLine MinimumInteractiveTopAlignmentLine{ ui::AlignmentLine::Merge::Min, true };
    inline constexpr ui::AlignmentLine MinimumInteractiveLeftAlignmentLine{ ui::AlignmentLine::Merge::Min, false };

    // ComposeMaterial3Flags
    namespace ComposeMaterial3Flags {
        inline bool isPrecisionPointerComponentSizingEnabled = false;
        inline bool isCheckboxStylingFixEnabled = false;
        inline bool isExpressiveListItemHeightBasedOnTextLinesFixEnabled = true;
        inline bool isSnackbarStylingFixEnabled = false;
        inline bool isBottomSheetPartiallyExpandedDeterministicEnabled = true;
        inline bool isModalBottomSheetStateRestorationFixEnabled = true;
        inline bool isUpdatedTimepickerToggleEnabled = true;
    }

    // TextSelectionBackgroundOpacity (selection highlight: primary at this alpha).
    inline constexpr float TextSelectionBackgroundOpacity = 0.4f;

}
