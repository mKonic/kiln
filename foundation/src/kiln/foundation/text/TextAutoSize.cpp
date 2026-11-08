#include "kiln/foundation/text/TextAutoSize.h"

#include <cmath>

namespace kiln::foundation {

    namespace {
        class AutoSizeStepBased final : public TextAutoSize {
        public:
            AutoSizeStepBased(TextUnit min, TextUnit max, TextUnit step) : m_Min(min), m_Max(max), m_Step(step) {
                if (m_Min.type == m_Max.type && m_Min.value > m_Max.value) m_Min = m_Max;
            }

            TextUnit GetFontSize(TextAutoSizeLayoutScope& scope, const ui::Constraints& constraints, const text::AnnotatedString& text) const override {
                const float stepSize = scope.ToPx(m_Step), smallest = scope.ToPx(m_Min), largest = scope.ToPx(m_Max);
                float min = smallest, max = largest, current = (min + max) / 2;
                while ((max - min) >= stepSize) {
                    if (DidOverflow(scope.PerformLayout(constraints, text, scope.ToSp(current)))) max = current;
                    else min = current;
                    current = (min + max) / 2;
                }
                // A whole number of steps above the minimum, then one more step if it still fits.
                current = std::floor((min - smallest) / stepSize) * stepSize + smallest;
                if (current + stepSize <= largest && !DidOverflow(scope.PerformLayout(constraints, text, scope.ToSp(current + stepSize))))
                    current += stepSize;
                return scope.ToSp(current);
            }

            bool Equals(const TextAutoSize& other) const override {
                const auto* o = dynamic_cast<const AutoSizeStepBased*>(&other);
                return o && o->m_Min == m_Min && o->m_Max == m_Max && o->m_Step == m_Step;
            }

        private:
            static bool DidOverflowBounds(const text::TextLayoutResult& r) { return r.DidOverflowWidth() || r.DidOverflowHeight(); }
            static bool DidOverflow(const text::TextLayoutResult& r) {
                using text::TextOverflow;
                const TextOverflow overflow = r.LayoutInput().overflow;
                if (overflow == TextOverflow::Clip || overflow == TextOverflow::Visible) return DidOverflowBounds(r);
                // Ellipses: start and middle ellipsize single lines only; end ellipsis the last line.
                const int lines = r.LineCount();
                if (lines == 0) return false;
                if (lines == 1) return r.IsLineEllipsized(0);
                if (overflow == TextOverflow::Ellipsis) return r.IsLineEllipsized(lines - 1);
                return DidOverflowBounds(r);
            }
            TextUnit m_Min, m_Max, m_Step;
        };
    }

    std::shared_ptr<const TextAutoSize> TextAutoSize::StepBased(TextUnit minFontSize, TextUnit maxFontSize, TextUnit stepSize) {
        // AutoSizeStepBased's checks (Compose throws).
        if (!minFontSize.IsSpecified() || !maxFontSize.IsSpecified() || !stepSize.IsSpecified()) return nullptr;
        if (stepSize.IsSp() && stepSize.value < 0.0001f) return nullptr;
        if (minFontSize.value < 0 || maxFontSize.value < 0) return nullptr;
        return std::make_shared<AutoSizeStepBased>(minFontSize, maxFontSize, stepSize);
    }

}
