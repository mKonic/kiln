#pragma once

// TextAutoSize (foundation text/TextAutoSize.kt, modifiers/TextAutoSizeLayoutScope.kt): picks
// the font size BasicText lays out with for its constraints. StepBased searches between a
// minimum and a maximum for the largest size, in steps, at which the text does not overflow.

#include "kiln/text/Paragraph.h"

#include <memory>

namespace kiln::foundation {

    using text::TextUnit;

    // TextAutoSizeLayoutScope: lays the text out at a font size (sp, or em of the style's size).
    class TextAutoSizeLayoutScope : public ui::Density {
    public:
        virtual ~TextAutoSizeLayoutScope() = default;
        virtual text::TextLayoutResult PerformLayout(const ui::Constraints& constraints, const text::AnnotatedString& text, TextUnit fontSize) = 0;
        // TextUnit.toPx: sp through the font scale, em against the style's font size.
        virtual float ToPx(TextUnit unit) const = 0;
        TextUnit ToSp(float px) const { return text::Sp(PxToSp(px)); }
    };

    class TextAutoSize {
    public:
        virtual ~TextAutoSize() = default;
        virtual TextUnit GetFontSize(TextAutoSizeLayoutScope& scope, const ui::Constraints& constraints, const text::AnnotatedString& text) const = 0;
        virtual bool Equals(const TextAutoSize& other) const = 0;

        // TextAutoSize.StepBased (min 12 sp, max 112 sp, step 0.25 sp). A minimum above the
        // maximum takes the maximum; an unspecified, negative or too small step gives null.
        static std::shared_ptr<const TextAutoSize> StepBased(TextUnit minFontSize = text::Sp(12), TextUnit maxFontSize = text::Sp(112),
                                                             TextUnit stepSize = text::Sp(0.25f));
    };
    using TextAutoSizePtr = std::shared_ptr<const TextAutoSize>;

}
