// TypographySamples.kt
#include "TypographySamples.h"
#include "Common.h"

#include <kiln/material3/MaterialTheme.h>

namespace catalog::samples {
    namespace {
        using kiln::text::FontFamily;
        using kiln::text::FontWeight;
        using kiln::text::TextStyle;

        TextStyle Style(float fontSizeSp, std::optional<FontWeight> weight = std::nullopt, std::optional<FontFamily> family = std::nullopt) {
            TextStyle s(fontSizeSp);
            s.span.fontWeight = weight;
            s.span.fontFamily = family;
            return s;
        }

        void Themed(const m3::Typography& typography, const char* display, const char* label, const char* body) {
            m3::MaterialThemeArgs args;
            args.typography = typography;
            m3::MaterialTheme(args, [display, label, body] {
                Column({}, [display, label, body] {
                    TextStyled(display, m3::MaterialTheme::typography().displayLarge);
                    TextStyled(label, m3::MaterialTheme::typography().labelLarge);
                    TextStyled(body, m3::MaterialTheme::typography().bodyLarge);
                });
            });
        }
    }

    void TypographySample() {
        m3::Typography::Overrides o;
        o.displayLarge = Style(96, FontWeight::W100());
        o.labelLarge = Style(14, FontWeight::W600());
        Themed(m3::Typography(FontFamily::Default(), o), "Display Large", "Label Large", "Body Large");
    }

    void TypographyCustomFontFamilySample() {
        m3::Typography::Overrides o;
        // font family is not defined, so the font family supplied to the typography is used
        o.displayLarge = Style(40);
        // font family is defined on the text style, so this will be used as it explicitly
        // overrides the font family defined on the typography
        o.labelLarge = Style(14, std::nullopt, FontFamily::Monospace());
        Themed(m3::Typography(FontFamily::Cursive(), o), "Cursive Display Large", "Monospace Label Large", "Cursive Body Large");
    }
}
