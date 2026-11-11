#include "kiln/material3/Typography.h"

#include "kiln/material3/tokens/TypographyKeyTokens.h"
#include "kiln/material3/tokens/TypographyTokens.h"

namespace kiln::material3 {

    namespace {
        // TextStyle?.withDefaultFontFamily
        std::optional<text::TextStyle> WithDefaultFontFamily(const std::optional<text::TextStyle>& s, const std::optional<text::FontFamily>& family) {
            if (!s || s->span.fontFamily) return s;
            text::TextStyle copy = *s;
            copy.span.fontFamily = family;
            return copy;
        }
    }

    Typography::Typography() {
        const tokens::TypographyTokens t;
        displayLarge = t.DisplayLarge();
        displayMedium = t.DisplayMedium();
        displaySmall = t.DisplaySmall();
        headlineLarge = t.HeadlineLarge();
        headlineMedium = t.HeadlineMedium();
        headlineSmall = t.HeadlineSmall();
        titleLarge = t.TitleLarge();
        titleMedium = t.TitleMedium();
        titleSmall = t.TitleSmall();
        bodyLarge = t.BodyLarge();
        bodyMedium = t.BodyMedium();
        bodySmall = t.BodySmall();
        labelLarge = t.LabelLarge();
        labelMedium = t.LabelMedium();
        labelSmall = t.LabelSmall();
        displayLargeEmphasized = displayLarge;
        displayMediumEmphasized = displayMedium;
        displaySmallEmphasized = displaySmall;
        headlineLargeEmphasized = headlineLarge;
        headlineMediumEmphasized = headlineMedium;
        headlineSmallEmphasized = headlineSmall;
        titleLargeEmphasized = titleLarge;
        titleMediumEmphasized = titleMedium;
        titleSmallEmphasized = titleSmall;
        bodyLargeEmphasized = bodyLarge;
        bodyMediumEmphasized = bodyMedium;
        bodySmallEmphasized = bodySmall;
        labelLargeEmphasized = labelLarge;
        labelMediumEmphasized = labelMedium;
        labelSmallEmphasized = labelSmall;
    }

    Typography::Typography(const text::FontFamily& fontFamily, const Overrides& o) {
        const tokens::TypographyTokens t(fontFamily);
        const std::optional<text::FontFamily> family = fontFamily;
        displayLarge = WithDefaultFontFamily(o.displayLarge, family).value_or(t.DisplayLarge());
        displayMedium = WithDefaultFontFamily(o.displayMedium, family).value_or(t.DisplayMedium());
        displaySmall = WithDefaultFontFamily(o.displaySmall, family).value_or(t.DisplaySmall());
        headlineLarge = WithDefaultFontFamily(o.headlineLarge, family).value_or(t.HeadlineLarge());
        headlineMedium = WithDefaultFontFamily(o.headlineMedium, family).value_or(t.HeadlineMedium());
        headlineSmall = WithDefaultFontFamily(o.headlineSmall, family).value_or(t.HeadlineSmall());
        titleLarge = WithDefaultFontFamily(o.titleLarge, family).value_or(t.TitleLarge());
        titleMedium = WithDefaultFontFamily(o.titleMedium, family).value_or(t.TitleMedium());
        titleSmall = WithDefaultFontFamily(o.titleSmall, family).value_or(t.TitleSmall());
        bodyLarge = WithDefaultFontFamily(o.bodyLarge, family).value_or(t.BodyLarge());
        bodyMedium = WithDefaultFontFamily(o.bodyMedium, family).value_or(t.BodyMedium());
        bodySmall = WithDefaultFontFamily(o.bodySmall, family).value_or(t.BodySmall());
        labelLarge = WithDefaultFontFamily(o.labelLarge, family).value_or(t.LabelLarge());
        labelMedium = WithDefaultFontFamily(o.labelMedium, family).value_or(t.LabelMedium());
        labelSmall = WithDefaultFontFamily(o.labelSmall, family).value_or(t.LabelSmall());
        displayLargeEmphasized = WithDefaultFontFamily(o.displayLargeEmphasized, family).value_or(t.DisplayLargeEmphasized());
        displayMediumEmphasized = WithDefaultFontFamily(o.displayMediumEmphasized, family).value_or(t.DisplayMediumEmphasized());
        displaySmallEmphasized = WithDefaultFontFamily(o.displaySmallEmphasized, family).value_or(t.DisplaySmallEmphasized());
        headlineLargeEmphasized = WithDefaultFontFamily(o.headlineLargeEmphasized, family).value_or(t.HeadlineLargeEmphasized());
        headlineMediumEmphasized = WithDefaultFontFamily(o.headlineMediumEmphasized, family).value_or(t.HeadlineMediumEmphasized());
        headlineSmallEmphasized = WithDefaultFontFamily(o.headlineSmallEmphasized, family).value_or(t.HeadlineSmallEmphasized());
        titleLargeEmphasized = WithDefaultFontFamily(o.titleLargeEmphasized, family).value_or(t.TitleLargeEmphasized());
        titleMediumEmphasized = WithDefaultFontFamily(o.titleMediumEmphasized, family).value_or(t.TitleMediumEmphasized());
        titleSmallEmphasized = WithDefaultFontFamily(o.titleSmallEmphasized, family).value_or(t.TitleSmallEmphasized());
        bodyLargeEmphasized = WithDefaultFontFamily(o.bodyLargeEmphasized, family).value_or(t.BodyLargeEmphasized());
        bodyMediumEmphasized = WithDefaultFontFamily(o.bodyMediumEmphasized, family).value_or(t.BodyMediumEmphasized());
        bodySmallEmphasized = WithDefaultFontFamily(o.bodySmallEmphasized, family).value_or(t.BodySmallEmphasized());
        labelLargeEmphasized = WithDefaultFontFamily(o.labelLargeEmphasized, family).value_or(t.LabelLargeEmphasized());
        labelMediumEmphasized = WithDefaultFontFamily(o.labelMediumEmphasized, family).value_or(t.LabelMediumEmphasized());
        labelSmallEmphasized = WithDefaultFontFamily(o.labelSmallEmphasized, family).value_or(t.LabelSmallEmphasized());
    }

    const text::TextStyle& FromToken(const Typography& t, tokens::TypographyToken token) {
        namespace K = tokens::TypographyKeyTokens;
        if (token == K::DisplayLarge) return t.displayLarge;
        if (token == K::DisplayMedium) return t.displayMedium;
        if (token == K::DisplaySmall) return t.displaySmall;
        if (token == K::HeadlineLarge) return t.headlineLarge;
        if (token == K::HeadlineMedium) return t.headlineMedium;
        if (token == K::HeadlineSmall) return t.headlineSmall;
        if (token == K::TitleLarge) return t.titleLarge;
        if (token == K::TitleMedium) return t.titleMedium;
        if (token == K::TitleSmall) return t.titleSmall;
        if (token == K::BodyLarge) return t.bodyLarge;
        if (token == K::BodyMedium) return t.bodyMedium;
        if (token == K::BodySmall) return t.bodySmall;
        if (token == K::LabelLarge) return t.labelLarge;
        if (token == K::LabelMedium) return t.labelMedium;
        if (token == K::LabelSmall) return t.labelSmall;
        if (token == K::DisplayLargeEmphasized) return t.displayLargeEmphasized;
        if (token == K::DisplayMediumEmphasized) return t.displayMediumEmphasized;
        if (token == K::DisplaySmallEmphasized) return t.displaySmallEmphasized;
        if (token == K::HeadlineLargeEmphasized) return t.headlineLargeEmphasized;
        if (token == K::HeadlineMediumEmphasized) return t.headlineMediumEmphasized;
        if (token == K::HeadlineSmallEmphasized) return t.headlineSmallEmphasized;
        if (token == K::TitleLargeEmphasized) return t.titleLargeEmphasized;
        if (token == K::TitleMediumEmphasized) return t.titleMediumEmphasized;
        if (token == K::TitleSmallEmphasized) return t.titleSmallEmphasized;
        if (token == K::BodyLargeEmphasized) return t.bodyLargeEmphasized;
        if (token == K::BodyMediumEmphasized) return t.bodyMediumEmphasized;
        if (token == K::BodySmallEmphasized) return t.bodySmallEmphasized;
        if (token == K::LabelLargeEmphasized) return t.labelLargeEmphasized;
        if (token == K::LabelMediumEmphasized) return t.labelMediumEmphasized;
        if (token == K::LabelSmallEmphasized) return t.labelSmallEmphasized;
        return t.bodyLarge;
    }

}
