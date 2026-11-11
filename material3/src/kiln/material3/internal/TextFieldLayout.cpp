#include "kiln/material3/internal/TextFieldLayout.h"

#include "kiln/material3/tokens/MotionTokens.h"

#include <algorithm>

namespace kiln::material3::internal {
    using ui::Constraints;
    using ui::Measurable;
    using ui::Placeable;

    namespace {
        Measurable* Find(std::span<Measurable* const> children, const char* id) {
            for (Measurable* m : children)
                if (const std::string* l = foundation::LayoutIdOf(*m); l && *l == id) return m;
            return nullptr;
        }
        int WidthOrZero(const Placeable* p) { return p ? p->Width() : 0; }
        int HeightOrZero(const Placeable* p) { return p ? p->Height() : 0; }
        // subtractConstraintSafely
        int SubtractSafely(int v, int n) { return v == Constraints::Infinity ? v : v - n; }
        int CenterVertically(int size, int space) { return ui::AlignmentV::CenterVertically().Align(size, space); }

        class FilledPolicy final : public ui::MeasurePolicy {
        public:
            explicit FilledPolicy(TextFieldLayoutParams p) : m_P(std::move(p)) {}

            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<Measurable* const> children, const Constraints& constraints) override {
                const float labelProgress = Progress();
                const int topPadding = s.RoundToPx(m_P.padding.top);
                const int bottomPadding = s.RoundToPx(m_P.padding.bottom);
                int occupiedH = 0, occupiedV = 0;
                Constraints loose = constraints;
                loose.minWidth = loose.minHeight = 0;

                Placeable* leading = nullptr;
                if (Measurable* m = Find(children, LeadingId)) leading = &m->Measure(loose);
                occupiedH += WidthOrZero(leading);
                occupiedV = std::max(occupiedV, HeightOrZero(leading));
                Placeable* trailing = nullptr;
                if (Measurable* m = Find(children, TrailingId)) trailing = &m->Measure(loose.Offset(-occupiedH, 0));
                occupiedH += WidthOrZero(trailing);
                occupiedV = std::max(occupiedV, HeightOrZero(trailing));
                Placeable* prefix = nullptr;
                if (Measurable* m = Find(children, PrefixId)) prefix = &m->Measure(loose.Offset(-occupiedH, 0));
                occupiedH += WidthOrZero(prefix);
                occupiedV = std::max(occupiedV, HeightOrZero(prefix));
                Placeable* suffix = nullptr;
                if (Measurable* m = Find(children, SuffixId)) suffix = &m->Measure(loose.Offset(-occupiedH, 0));
                occupiedH += WidthOrZero(suffix);
                occupiedV = std::max(occupiedV, HeightOrZero(suffix));

                const bool above = m_P.placement == LabelPlacement::Above;
                Measurable* labelMeasurable = Find(children, LabelId);
                Placeable* label = nullptr;
                int labelIntrinsicHeight = 0;
                if (!above) {
                    if (labelMeasurable) label = &labelMeasurable->Measure(loose.Offset(-occupiedH, -bottomPadding));
                } else {
                    // Measured after the rest; its intrinsic height reserves the space.
                    labelIntrinsicHeight = labelMeasurable ? labelMeasurable->MinIntrinsicHeight(constraints.minWidth) : 0;
                }
                Measurable* supportingMeasurable = Find(children, SupportingId);
                const int supportingIntrinsicHeight = supportingMeasurable ? supportingMeasurable->MinIntrinsicHeight(constraints.minWidth) : 0;
                const int labelHeightOrIntrinsic = HeightOrZero(label) + labelIntrinsicHeight;

                const int effectiveTopOffset = topPadding + labelHeightOrIntrinsic;
                Constraints textConstraints = constraints;
                textConstraints.minHeight = 0;
                textConstraints = textConstraints.Offset(-occupiedH, -effectiveTopOffset - bottomPadding - supportingIntrinsicHeight);
                Placeable& text = Find(children, TextFieldId)->Measure(textConstraints);
                Constraints placeholderConstraints = textConstraints;
                placeholderConstraints.minWidth = 0;
                Placeable* placeholder = nullptr;
                if (Measurable* m = Find(children, PlaceholderId)) placeholder = &m->Measure(placeholderConstraints);
                occupiedV = std::max(occupiedV, std::max(text.Height(), HeightOrZero(placeholder)) + effectiveTopOffset + bottomPadding);

                const int width = CalculateWidth(WidthOrZero(leading), WidthOrZero(trailing), WidthOrZero(prefix), WidthOrZero(suffix), text.Width(),
                                                 WidthOrZero(label), WidthOrZero(placeholder), constraints);
                if (above && labelMeasurable) {
                    Constraints lc = loose;
                    lc.maxHeight = labelIntrinsicHeight;
                    lc.maxWidth = width;
                    label = &labelMeasurable->Measure(lc);
                }
                Placeable* supporting = nullptr;
                if (supportingMeasurable) {
                    Constraints sc = loose.Offset(0, -occupiedV);
                    sc.minHeight = 0;
                    sc.maxWidth = width;
                    supporting = &supportingMeasurable->Measure(sc);
                }
                const int supportingHeight = HeightOrZero(supporting);
                const int totalHeight = CalculateHeight(s, text.Height(), HeightOrZero(label), HeightOrZero(leading), HeightOrZero(trailing),
                                                        HeightOrZero(prefix), HeightOrZero(suffix), HeightOrZero(placeholder), supportingHeight,
                                                        constraints, above, labelProgress);
                const int height = totalHeight - supportingHeight - (above ? HeightOrZero(label) : 0);
                Placeable& container = Find(children, ContainerId)->Measure(Constraints{
                    width != Constraints::Infinity ? width : 0, width, height != Constraints::Infinity ? height : 0, height });

                const ui::LayoutDirection dir = s.Direction();
                const int halfHeight = s.RoundToPx(m_P.minimizedLabelHalfHeight);
                return s.Layout(width, totalHeight, [=, this, &text, &container](ui::PlacementScope& place) {
                    if (label) {
                        const int labelStartY = above ? 0 : m_P.singleLine ? CenterVertically(label->Height(), height) : topPadding + halfHeight;
                        const int labelEndY = above ? 0 : topPadding;
                        PlaceWithLabel(place, width, totalHeight, text, *label, placeholder, leading, trailing, prefix, suffix, container, supporting,
                                       labelStartY, labelEndY, above, labelProgress, topPadding + (above ? 0 : label->Height()), dir);
                    } else {
                        PlaceWithoutLabel(place, width, totalHeight, text, placeholder, leading, trailing, prefix, suffix, container, supporting, topPadding);
                    }
                });
            }

            int MaxIntrinsicHeight(ui::MeasureScope& s, std::span<Measurable* const> c, int width) override {
                return IntrinsicHeight(s, c, width, [](Measurable& m, int w) { return m.MaxIntrinsicHeight(w); });
            }
            int MinIntrinsicHeight(ui::MeasureScope& s, std::span<Measurable* const> c, int width) override {
                return IntrinsicHeight(s, c, width, [](Measurable& m, int w) { return m.MinIntrinsicHeight(w); });
            }
            int MaxIntrinsicWidth(ui::MeasureScope&, std::span<Measurable* const> c, int height) override {
                return IntrinsicWidth(c, height, [](Measurable& m, int h) { return m.MaxIntrinsicWidth(h); });
            }
            int MinIntrinsicWidth(ui::MeasureScope&, std::span<Measurable* const> c, int height) override {
                return IntrinsicWidth(c, height, [](Measurable& m, int h) { return m.MinIntrinsicWidth(h); });
            }

        private:
            float Progress() const { return m_P.labelProgress ? m_P.labelProgress() : 1.0f; }

            template<typename F> int IntrinsicWidth(std::span<Measurable* const> c, int height, F measure) const {
                const auto of = [&](const char* id) { Measurable* m = Find(c, id); return m ? measure(*m, height) : 0; };
                return CalculateWidth(of(LeadingId), of(TrailingId), of(PrefixId), of(SuffixId), measure(*Find(c, TextFieldId), height), of(LabelId),
                                      of(PlaceholderId), Constraints{});
            }

            template<typename F> int IntrinsicHeight(ui::MeasureScope& s, std::span<Measurable* const> c, int width, F measure) const {
                int remaining = width;
                int leadingH = 0, trailingH = 0, prefixH = 0, suffixH = 0, placeholderH = 0, supportingH = 0, labelH = 0;
                if (Measurable* m = Find(c, LeadingId)) {
                    remaining = SubtractSafely(remaining, m->MaxIntrinsicWidth(Constraints::Infinity));
                    leadingH = measure(*m, width);
                }
                if (Measurable* m = Find(c, TrailingId)) {
                    remaining = SubtractSafely(remaining, m->MaxIntrinsicWidth(Constraints::Infinity));
                    trailingH = measure(*m, width);
                }
                if (Measurable* m = Find(c, LabelId)) labelH = measure(*m, remaining);
                if (Measurable* m = Find(c, PrefixId)) {
                    prefixH = measure(*m, remaining);
                    remaining = SubtractSafely(remaining, m->MaxIntrinsicWidth(Constraints::Infinity));
                }
                if (Measurable* m = Find(c, SuffixId)) {
                    suffixH = measure(*m, remaining);
                    remaining = SubtractSafely(remaining, m->MaxIntrinsicWidth(Constraints::Infinity));
                }
                const int textH = measure(*Find(c, TextFieldId), remaining);
                if (Measurable* m = Find(c, PlaceholderId)) placeholderH = measure(*m, remaining);
                if (Measurable* m = Find(c, SupportingId)) supportingH = measure(*m, width);
                return CalculateHeight(s, textH, labelH, leadingH, trailingH, prefixH, suffixH, placeholderH, supportingH, Constraints{},
                                       m_P.placement == LabelPlacement::Above, Progress());
            }

            static int CalculateWidth(int leading, int trailing, int prefix, int suffix, int text, int label, int placeholder, const Constraints& c) {
                const int affix = prefix + suffix;
                // The prefix and suffix do not apply to the label.
                const int middle = std::max({ text + affix, placeholder + affix, label });
                return c.ConstrainWidth(leading + middle + trailing);
            }

            int CalculateHeight(ui::MeasureScope& s, int text, int label, int leading, int trailing, int prefix, int suffix, int placeholder,
                                int supporting, const Constraints& c, bool above, float progress) const {
                const int verticalPadding = s.RoundToPx(ui::Dp(m_P.padding.top.value + m_P.padding.bottom.value));
                const int inputField = std::max({ text, placeholder, prefix, suffix, above ? 0 : LerpInt(label, 0, progress) });
                int nonOverlappedLabel = 0;
                if (label > 0 && !above) {
                    // The label moves from over the input to above it; the unfocused height keeps a
                    // baseline so the field does not jump during the animation.
                    const float eased = tokens::MotionTokens::EasingEmphasizedAccelerateCubicBezier.ToEasing().Transform(progress);
                    nonOverlappedLabel = std::max(s.RoundToPx(ui::Dp(m_P.minimizedLabelHalfHeight.value * 2)), LerpInt(0, label, eased));
                }
                const int middle = verticalPadding + nonOverlappedLabel + inputField;
                return c.ConstrainHeight((above ? label : 0) + std::max({ leading, trailing, middle }) + supporting);
            }

            void PlaceWithLabel(ui::PlacementScope& place, int width, int totalHeight, Placeable& text, Placeable& label, Placeable* placeholder,
                                Placeable* leading, Placeable* trailing, Placeable* prefix, Placeable* suffix, Placeable& container,
                                Placeable* supporting, int labelStartY, int labelEndY, bool above, float progress, int textPosition,
                                ui::LayoutDirection dir) const {
                const int yOffset = above ? label.Height() : 0;
                place.Place(container, 0, yOffset);
                // The visual height: without an Above label and the supporting text.
                const int height = totalHeight - HeightOrZero(supporting) - (above ? label.Height() : 0);
                if (leading) place.PlaceRelative(*leading, 0, yOffset + CenterVertically(leading->Height(), height));
                const int labelY = LerpInt(labelStartY, labelEndY, progress);
                if (above) {
                    // Not placeRelative: the alignment handles RTL.
                    place.Place(label, m_P.minimizedAlignment.Align(label.Width(), width, dir), labelY);
                } else {
                    const int leftIconWidth = dir == ui::LayoutDirection::Ltr ? WidthOrZero(leading) : WidthOrZero(trailing);
                    const int space = width - WidthOrZero(leading) - WidthOrZero(trailing);
                    const int startX = m_P.expandedAlignment.Align(label.Width(), space, dir) + leftIconWidth;
                    const int endX = m_P.minimizedAlignment.Align(label.Width(), space, dir) + leftIconWidth;
                    place.Place(label, LerpInt(startX, endX, progress), labelY);
                }
                if (prefix) place.PlaceRelative(*prefix, WidthOrZero(leading), yOffset + textPosition);
                const int textX = WidthOrZero(leading) + WidthOrZero(prefix);
                place.PlaceRelative(text, textX, yOffset + textPosition);
                if (placeholder) place.PlaceRelative(*placeholder, textX, yOffset + textPosition);
                if (suffix) place.PlaceRelative(*suffix, width - WidthOrZero(trailing) - suffix->Width(), yOffset + textPosition);
                if (trailing) place.PlaceRelative(*trailing, width - trailing->Width(), yOffset + CenterVertically(trailing->Height(), height));
                if (supporting) place.PlaceRelative(*supporting, 0, yOffset + height);
            }

            void PlaceWithoutLabel(ui::PlacementScope& place, int width, int totalHeight, Placeable& text, Placeable* placeholder, Placeable* leading,
                                   Placeable* trailing, Placeable* prefix, Placeable* suffix, Placeable& container, Placeable* supporting,
                                   int topPadding) const {
                place.Place(container, 0, 0);
                const int height = totalHeight - HeightOrZero(supporting);
                if (leading) place.PlaceRelative(*leading, 0, CenterVertically(leading->Height(), height));
                // One line without a label: centred; more lines: at the top padding.
                const auto y = [&](const Placeable& p) { return m_P.singleLine ? CenterVertically(p.Height(), height) : topPadding; };
                if (prefix) place.PlaceRelative(*prefix, WidthOrZero(leading), y(*prefix));
                const int textX = WidthOrZero(leading) + WidthOrZero(prefix);
                place.PlaceRelative(text, textX, y(text));
                if (placeholder) place.PlaceRelative(*placeholder, textX, y(*placeholder));
                if (suffix) place.PlaceRelative(*suffix, width - WidthOrZero(trailing) - suffix->Width(), y(*suffix));
                if (trailing) place.PlaceRelative(*trailing, width - trailing->Width(), CenterVertically(trailing->Height(), height));
                if (supporting) place.PlaceRelative(*supporting, 0, height);
            }

            TextFieldLayoutParams m_P;
        };

        float LerpF(float a, float b, float t) { return a + (b - a) * t; }
        int RoundF(float v) { return int(std::floor(v + 0.5f)); }   // roundToInt

        class OutlinedPolicy final : public ui::MeasurePolicy {
        public:
            explicit OutlinedPolicy(TextFieldLayoutParams p) : m_P(std::move(p)) {}

            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<Measurable* const> children, const Constraints& constraints) override {
                const float labelProgress = Progress();
                int occupiedH = 0, occupiedV = 0;
                const int bottomPadding = s.RoundToPx(m_P.padding.bottom);
                Constraints relaxed = constraints;
                relaxed.minWidth = relaxed.minHeight = 0;

                Placeable* leading = nullptr;
                if (Measurable* m = Find(children, LeadingId)) leading = &m->Measure(relaxed);
                occupiedH += WidthOrZero(leading);
                occupiedV = std::max(occupiedV, HeightOrZero(leading));
                Placeable* trailing = nullptr;
                if (Measurable* m = Find(children, TrailingId)) trailing = &m->Measure(relaxed.Offset(-occupiedH, 0));
                occupiedH += WidthOrZero(trailing);
                occupiedV = std::max(occupiedV, HeightOrZero(trailing));
                Placeable* prefix = nullptr;
                if (Measurable* m = Find(children, PrefixId)) prefix = &m->Measure(relaxed.Offset(-occupiedH, 0));
                occupiedH += WidthOrZero(prefix);
                occupiedV = std::max(occupiedV, HeightOrZero(prefix));
                Placeable* suffix = nullptr;
                if (Measurable* m = Find(children, SuffixId)) suffix = &m->Measure(relaxed.Offset(-occupiedH, 0));
                occupiedH += WidthOrZero(suffix);
                occupiedV = std::max(occupiedV, HeightOrZero(suffix));

                const bool above = m_P.placement == LabelPlacement::Above;
                Measurable* labelMeasurable = Find(children, LabelId);
                Placeable* label = nullptr;
                int labelIntrinsicHeight = 0;
                const auto reportLabel = [&] {
                    if (m_P.onLabelMeasured) m_P.onLabelMeasured(label ? ui::Size{ float(label->Width()), float(label->Height()) } : ui::Size{});
                };
                if (!above) {
                    const int horizontalPadding = s.RoundToPx(m_P.padding.start) + s.RoundToPx(m_P.padding.end);
                    // In the middle of the field, then in the outline.
                    const int offset = LerpInt(occupiedH + horizontalPadding, horizontalPadding, labelProgress);
                    if (labelMeasurable) label = &labelMeasurable->Measure(relaxed.Offset(-offset, -bottomPadding));
                    reportLabel();
                } else {
                    labelIntrinsicHeight = labelMeasurable ? labelMeasurable->MinIntrinsicHeight(constraints.minWidth) : 0;
                }
                Measurable* supportingMeasurable = Find(children, SupportingId);
                const int supportingIntrinsicHeight = supportingMeasurable ? supportingMeasurable->MinIntrinsicHeight(constraints.minWidth) : 0;

                const int paddingTop = s.RoundToPx(m_P.padding.top);
                const int topPadding = above ? paddingTop : std::max(HeightOrZero(label) / 2, paddingTop);
                Constraints textConstraints = constraints.Offset(-occupiedH, -bottomPadding - topPadding - labelIntrinsicHeight - supportingIntrinsicHeight);
                textConstraints.minHeight = 0;
                Placeable& text = Find(children, TextFieldId)->Measure(textConstraints);
                Constraints placeholderConstraints = textConstraints;
                placeholderConstraints.minWidth = 0;
                Placeable* placeholder = nullptr;
                if (Measurable* m = Find(children, PlaceholderId)) placeholder = &m->Measure(placeholderConstraints);
                occupiedV = std::max(occupiedV, std::max(text.Height(), HeightOrZero(placeholder)) + topPadding + bottomPadding);

                const int width = CalculateWidth(s, WidthOrZero(leading), WidthOrZero(trailing), WidthOrZero(prefix), WidthOrZero(suffix), text.Width(),
                                                 WidthOrZero(label), WidthOrZero(placeholder), constraints, labelProgress);
                if (above) {
                    if (labelMeasurable) {
                        Constraints lc = relaxed;
                        lc.maxHeight = labelIntrinsicHeight;
                        lc.maxWidth = width;
                        label = &labelMeasurable->Measure(lc);
                    }
                    reportLabel();
                }
                Placeable* supporting = nullptr;
                if (supportingMeasurable) {
                    Constraints sc = relaxed.Offset(0, -occupiedV);
                    sc.minHeight = 0;
                    sc.maxWidth = width;
                    supporting = &supportingMeasurable->Measure(sc);
                }
                const int supportingHeight = HeightOrZero(supporting);
                const int totalHeight = CalculateHeight(s, HeightOrZero(leading), HeightOrZero(trailing), HeightOrZero(prefix), HeightOrZero(suffix),
                                                        text.Height(), HeightOrZero(label), HeightOrZero(placeholder), supportingHeight, constraints,
                                                        above, labelProgress);
                const int height = totalHeight - supportingHeight - (above ? HeightOrZero(label) : 0);
                Placeable& container = Find(children, ContainerId)->Measure(Constraints{
                    width != Constraints::Infinity ? width : 0, width, height != Constraints::Infinity ? height : 0, height });

                const ui::LayoutDirection dir = s.Direction();
                const float density = s.density;
                const float iconPadding = s.ToPx(m_P.horizontalIconPadding);
                return s.Layout(width, totalHeight, [=, this, &text, &container](ui::PlacementScope& place) {
                    Place(place, totalHeight, width, leading, trailing, prefix, suffix, text, label, placeholder, container, supporting, density, dir,
                          above, labelProgress, iconPadding);
                });
            }

            int MaxIntrinsicHeight(ui::MeasureScope& s, std::span<Measurable* const> c, int width) override {
                return IntrinsicHeight(s, c, width, [](Measurable& m, int w) { return m.MaxIntrinsicHeight(w); });
            }
            int MinIntrinsicHeight(ui::MeasureScope& s, std::span<Measurable* const> c, int width) override {
                return IntrinsicHeight(s, c, width, [](Measurable& m, int w) { return m.MinIntrinsicHeight(w); });
            }
            int MaxIntrinsicWidth(ui::MeasureScope& s, std::span<Measurable* const> c, int height) override {
                return IntrinsicWidth(s, c, height, [](Measurable& m, int h) { return m.MaxIntrinsicWidth(h); });
            }
            int MinIntrinsicWidth(ui::MeasureScope& s, std::span<Measurable* const> c, int height) override {
                return IntrinsicWidth(s, c, height, [](Measurable& m, int h) { return m.MinIntrinsicWidth(h); });
            }

        private:
            float Progress() const { return m_P.labelProgress ? m_P.labelProgress() : 1.0f; }

            template<typename F> int IntrinsicWidth(ui::MeasureScope& s, std::span<Measurable* const> c, int height, F measure) const {
                const auto of = [&](const char* id) { Measurable* m = Find(c, id); return m ? measure(*m, height) : 0; };
                return CalculateWidth(s, of(LeadingId), of(TrailingId), of(PrefixId), of(SuffixId), measure(*Find(c, TextFieldId), height), of(LabelId),
                                      of(PlaceholderId), Constraints{}, Progress());
            }

            template<typename F> int IntrinsicHeight(ui::MeasureScope& s, std::span<Measurable* const> c, int width, F measure) const {
                const float progress = Progress();
                int remaining = width;
                int leadingH = 0, trailingH = 0, prefixH = 0, suffixH = 0, placeholderH = 0, supportingH = 0, labelH = 0;
                if (Measurable* m = Find(c, LeadingId)) {
                    remaining = SubtractSafely(remaining, m->MaxIntrinsicWidth(Constraints::Infinity));
                    leadingH = measure(*m, width);
                }
                if (Measurable* m = Find(c, TrailingId)) {
                    remaining = SubtractSafely(remaining, m->MaxIntrinsicWidth(Constraints::Infinity));
                    trailingH = measure(*m, width);
                }
                if (Measurable* m = Find(c, LabelId)) labelH = measure(*m, LerpInt(remaining, width, progress));
                if (Measurable* m = Find(c, PrefixId)) {
                    prefixH = measure(*m, remaining);
                    remaining = SubtractSafely(remaining, m->MaxIntrinsicWidth(Constraints::Infinity));
                }
                if (Measurable* m = Find(c, SuffixId)) {
                    suffixH = measure(*m, remaining);
                    remaining = SubtractSafely(remaining, m->MaxIntrinsicWidth(Constraints::Infinity));
                }
                const int textH = measure(*Find(c, TextFieldId), remaining);
                if (Measurable* m = Find(c, PlaceholderId)) placeholderH = measure(*m, remaining);
                if (Measurable* m = Find(c, SupportingId)) supportingH = measure(*m, width);
                return CalculateHeight(s, leadingH, trailingH, prefixH, suffixH, textH, labelH, placeholderH, supportingH, Constraints{},
                                       m_P.placement == LabelPlacement::Above, progress);
            }

            int CalculateWidth(ui::MeasureScope& s, int leading, int trailing, int prefix, int suffix, int text, int label, int placeholder,
                               const Constraints& c, float progress) const {
                const int affix = prefix + suffix;
                const int middle = std::max({ text + affix, placeholder + affix, LerpInt(label, 0, progress) });
                const int wrapped = leading + middle + trailing;
                const float labelHorizontalPadding = s.ToPx(ui::Dp(m_P.padding.start.value + m_P.padding.end.value));
                const int focusedLabelWidth = RoundF((float(label) + labelHorizontalPadding) * progress);
                return c.ConstrainWidth(std::max(wrapped, focusedLabelWidth));
            }

            int CalculateHeight(ui::MeasureScope& s, int leading, int trailing, int prefix, int suffix, int text, int label, int placeholder,
                                int supporting, const Constraints& c, bool above, float progress) const {
                const int inputField = std::max({ text, placeholder, prefix, suffix, above ? 0 : LerpInt(label, 0, progress) });
                const float topPadding = s.ToPx(m_P.padding.top);
                const float actualTopPadding = above ? topPadding : LerpF(topPadding, std::max(topPadding, float(label) / 2), progress);
                const float bottomPadding = s.ToPx(m_P.padding.bottom);
                const float middle = actualTopPadding + float(inputField) + bottomPadding;
                return c.ConstrainHeight((above ? label : 0) + std::max({ leading, trailing, RoundF(middle) }) + supporting);
            }

            void Place(ui::PlacementScope& place, int totalHeight, int width, Placeable* leading, Placeable* trailing, Placeable* prefix,
                       Placeable* suffix, Placeable& text, Placeable* label, Placeable* placeholder, Placeable& container, Placeable* supporting,
                       float density, ui::LayoutDirection dir, bool above, float progress, float iconPadding) const {
                const int yOffset = above ? HeightOrZero(label) : 0;
                place.Place(container, 0, yOffset);
                const int height = totalHeight - HeightOrZero(supporting) - (above ? HeightOrZero(label) : 0);
                const int topPadding = RoundF(m_P.padding.top.value * density);
                if (leading) place.PlaceRelative(*leading, 0, yOffset + CenterVertically(leading->Height(), height));
                if (label) {
                    // Centred in a single line field before it moves into the outline.
                    const int startY = above ? 0 : m_P.singleLine ? CenterVertically(label->Height(), height) : topPadding;
                    const int endY = above ? 0 : -(label->Height() / 2);
                    const int y = LerpInt(startY, endY, progress);
                    if (above) {
                        place.Place(*label, m_P.minimizedAlignment.Align(label->Width(), width, dir), y);
                    } else {
                        const bool ltr = dir == ui::LayoutDirection::Ltr;
                        // calculateStartPadding / calculateEndPadding (absolute padding swaps in RTL).
                        const float start = (m_P.padding.rtlAware ? m_P.padding.start : (ltr ? m_P.padding.start : m_P.padding.end)).value * density;
                        const float end = (m_P.padding.rtlAware ? m_P.padding.end : (ltr ? m_P.padding.end : m_P.padding.start)).value * density;
                        const float leadingPlusPadding = leading ? float(leading->Width()) + std::max(start - iconPadding, 0.0f) : start;
                        const float trailingPlusPadding = trailing ? float(trailing->Width()) + std::max(end - iconPadding, 0.0f) : end;
                        const float leftPadding = ltr ? start : end;
                        const float leftIconPlusPadding = ltr ? leadingPlusPadding : trailingPlusPadding;
                        const float startX = float(m_P.expandedAlignment.Align(label->Width(), width - RoundF(leadingPlusPadding + trailingPlusPadding), dir)) +
                                             leftIconPlusPadding;
                        const float endX = float(m_P.minimizedAlignment.Align(label->Width(), width - RoundF(start + end), dir)) + leftPadding;
                        place.Place(*label, RoundF(LerpF(startX, endX, progress)), y);
                    }
                }
                const auto y = [&](const Placeable& p) {
                    const int def = yOffset + (m_P.singleLine ? CenterVertically(p.Height(), height) : topPadding);
                    // Below a label in the outline.
                    return above ? def : std::max(def, HeightOrZero(label) / 2);
                };
                if (prefix) place.PlaceRelative(*prefix, WidthOrZero(leading), y(*prefix));
                const int textX = WidthOrZero(leading) + WidthOrZero(prefix);
                place.PlaceRelative(text, textX, y(text));
                if (placeholder) place.PlaceRelative(*placeholder, textX, y(*placeholder));
                if (suffix) place.PlaceRelative(*suffix, width - WidthOrZero(trailing) - suffix->Width(), y(*suffix));
                if (trailing) place.PlaceRelative(*trailing, width - trailing->Width(), yOffset + CenterVertically(trailing->Height(), height));
                if (supporting) place.PlaceRelative(*supporting, 0, yOffset + height);
            }

            TextFieldLayoutParams m_P;
        };
    }

    std::shared_ptr<ui::MeasurePolicy> TextFieldMeasurePolicy(TextFieldLayoutParams p) { return std::make_shared<FilledPolicy>(std::move(p)); }
    std::shared_ptr<ui::MeasurePolicy> OutlinedTextFieldMeasurePolicy(TextFieldLayoutParams p) { return std::make_shared<OutlinedPolicy>(std::move(p)); }

}
