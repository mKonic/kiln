#include "kiln/ui/Layout.h"

#include <cmath>

namespace kiln::ui {

    namespace intrinsics {

        namespace {
            constexpr int kLargeDimension = (1 << 15) - 1;   // Layout.kt:294

            class EmptyPlaceable final : public Placeable {
            public:
                EmptyPlaceable(int w, int h) { SetMeasured({ w, h }, Constraints{}); }
                void PlaceAt(IntOffset, float) override {}
            };

            // DefaultIntrinsicMeasurable (LayoutModifier.kt:168-212).
            class Stand final : public Measurable {
            public:
                Stand(Measurable& m, Kind k) : m_M(m), m_K(k) {}
                Placeable& Measure(const Constraints& c) override {
                    if (m_K == Kind::MinWidth || m_K == Kind::MaxWidth) {
                        const int w = m_K == Kind::MaxWidth ? m_M.MaxIntrinsicWidth(c.maxHeight) : m_M.MinIntrinsicWidth(c.maxHeight);
                        m_P.emplace(w, c.HasBoundedHeight() ? c.maxHeight : kLargeDimension);
                    } else {
                        const int h = m_K == Kind::MaxHeight ? m_M.MaxIntrinsicHeight(c.maxWidth) : m_M.MinIntrinsicHeight(c.maxWidth);
                        m_P.emplace(c.HasBoundedWidth() ? c.maxWidth : kLargeDimension, h);
                    }
                    return *m_P;
                }
                int MinIntrinsicWidth(int h) override { return m_M.MinIntrinsicWidth(h); }
                int MaxIntrinsicWidth(int h) override { return m_M.MaxIntrinsicWidth(h); }
                int MinIntrinsicHeight(int w) override { return m_M.MinIntrinsicHeight(w); }
                int MaxIntrinsicHeight(int w) override { return m_M.MaxIntrinsicHeight(w); }
                const ParentData& GetParentData() const override { return m_M.GetParentData(); }
            private:
                Measurable& m_M;
                Kind m_K;
                std::optional<EmptyPlaceable> m_P;
            };
        }

        int Measure(Kind kind, int cross, std::span<Measurable* const> children,
                    const std::function<MeasureResult(std::span<Measurable* const>, const Constraints&)>& measure) {
            std::vector<std::unique_ptr<Stand>> stands;
            std::vector<Measurable*> ptrs;
            stands.reserve(children.size());
            for (Measurable* m : children) {
                stands.push_back(std::make_unique<Stand>(*m, kind));
                ptrs.push_back(stands.back().get());
            }
            const bool width = kind == Kind::MinWidth || kind == Kind::MaxWidth;
            Constraints c;
            if (width) c.maxHeight = cross; else c.maxWidth = cross;
            const MeasureResult r = measure(ptrs, c);
            return width ? r.width : r.height;
        }
    }

    using intrinsics::Kind;

    int MeasurePolicy::MinIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ch, int h) {
        return intrinsics::Measure(Kind::MinWidth, h, ch, [&](auto m, auto& c) { return Measure(s, m, c); });
    }
    int MeasurePolicy::MaxIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ch, int h) {
        return intrinsics::Measure(Kind::MaxWidth, h, ch, [&](auto m, auto& c) { return Measure(s, m, c); });
    }
    int MeasurePolicy::MinIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> ch, int w) {
        return intrinsics::Measure(Kind::MinHeight, w, ch, [&](auto m, auto& c) { return Measure(s, m, c); });
    }
    int MeasurePolicy::MaxIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> ch, int w) {
        return intrinsics::Measure(Kind::MaxHeight, w, ch, [&](auto m, auto& c) { return Measure(s, m, c); });
    }

    namespace {
        int ThroughModifier(LayoutModifier& mod, MeasureScope& s, Measurable& wrapped, Kind kind, int cross) {
            Measurable* one[] = { &wrapped };
            return intrinsics::Measure(kind, cross, one, [&](std::span<Measurable* const> m, const Constraints& c) {
                return mod.Measure(s, *m[0], c);
            });
        }
    }

    int LayoutModifier::MinIntrinsicWidth(MeasureScope& s, Measurable& w, int h) { return ThroughModifier(*this, s, w, Kind::MinWidth, h); }
    int LayoutModifier::MaxIntrinsicWidth(MeasureScope& s, Measurable& w, int h) { return ThroughModifier(*this, s, w, Kind::MaxWidth, h); }
    int LayoutModifier::MinIntrinsicHeight(MeasureScope& s, Measurable& w, int width) { return ThroughModifier(*this, s, w, Kind::MinHeight, width); }
    int LayoutModifier::MaxIntrinsicHeight(MeasureScope& s, Measurable& w, int width) { return ThroughModifier(*this, s, w, Kind::MaxHeight, width); }

    MeasureResult ApproachLayoutModifier::Measure(MeasureScope& scope, Measurable& wrapped, const Constraints& c) {
        Placeable& p = wrapped.Measure(c);
        return scope.Layout(p.Width(), p.Height(), [&p](PlacementScope& s) { s.Place(p, 0, 0); });
    }

    namespace {
        // NodeMeasuringIntrinsics with an ApproachMeasureBlock.
        int ThroughApproach(ApproachLayoutModifier& mod, ApproachMeasureScope* s, Measurable& wrapped, Kind kind, int cross) {
            if (!s) {
                switch (kind) {
                    case Kind::MinWidth: return wrapped.MinIntrinsicWidth(cross);
                    case Kind::MaxWidth: return wrapped.MaxIntrinsicWidth(cross);
                    case Kind::MinHeight: return wrapped.MinIntrinsicHeight(cross);
                    case Kind::MaxHeight: return wrapped.MaxIntrinsicHeight(cross);
                }
            }
            Measurable* one[] = { &wrapped };
            return intrinsics::Measure(kind, cross, one, [&](std::span<Measurable* const> m, const Constraints& c) {
                return mod.ApproachMeasure(*s, *m[0], c);
            });
        }
    }

    int ApproachLayoutModifier::MinApproachIntrinsicWidth(ApproachMeasureScope* s, Measurable& w, int h) { return ThroughApproach(*this, s, w, Kind::MinWidth, h); }
    int ApproachLayoutModifier::MaxApproachIntrinsicWidth(ApproachMeasureScope* s, Measurable& w, int h) { return ThroughApproach(*this, s, w, Kind::MaxWidth, h); }
    int ApproachLayoutModifier::MinApproachIntrinsicHeight(ApproachMeasureScope* s, Measurable& w, int width) { return ThroughApproach(*this, s, w, Kind::MinHeight, width); }
    int ApproachLayoutModifier::MaxApproachIntrinsicHeight(ApproachMeasureScope* s, Measurable& w, int width) { return ThroughApproach(*this, s, w, Kind::MaxHeight, width); }

    namespace {
        // mergeRulerValues: the largest (or smallest) value among the rulers that have one.
        template<typename R>
        float Merge(PlacementScope& s, const std::vector<R>& rulers, bool greater, float defaultValue) {
            float value = NAN;
            for (const R& r : rulers) {
                const float next = s.Current(r, NAN);
                if (std::isnan(value) || (greater == (next > value))) value = next;
            }
            return std::isnan(value) ? defaultValue : value;
        }
    }

    VerticalRuler VerticalRuler::MaxOf(std::vector<VerticalRuler> r) {
        return Derived([r](PlacementScope& s, float d) { return Merge(s, r, true, d); });
    }
    VerticalRuler VerticalRuler::MinOf(std::vector<VerticalRuler> r) {
        return Derived([r](PlacementScope& s, float d) { return Merge(s, r, false, d); });
    }
    HorizontalRuler HorizontalRuler::MaxOf(std::vector<HorizontalRuler> r) {
        return Derived([r](PlacementScope& s, float d) { return Merge(s, r, true, d); });
    }
    HorizontalRuler HorizontalRuler::MinOf(std::vector<HorizontalRuler> r) {
        return Derived([r](PlacementScope& s, float d) { return Merge(s, r, false, d); });
    }

    RectRulers RectRulers::InnermostOf(const std::vector<RectRulers>& rulers) {
        std::vector<VerticalRuler> l, r;
        std::vector<HorizontalRuler> t, b;
        for (const auto& x : rulers) { l.push_back(x.left); t.push_back(x.top); r.push_back(x.right); b.push_back(x.bottom); }
        return { VerticalRuler::MaxOf(l), HorizontalRuler::MaxOf(t), VerticalRuler::MinOf(r), HorizontalRuler::MinOf(b) };
    }
    RectRulers RectRulers::OutermostOf(const std::vector<RectRulers>& rulers) {
        std::vector<VerticalRuler> l, r;
        std::vector<HorizontalRuler> t, b;
        for (const auto& x : rulers) { l.push_back(x.left); t.push_back(x.top); r.push_back(x.right); b.push_back(x.bottom); }
        return { VerticalRuler::MinOf(l), HorizontalRuler::MinOf(t), VerticalRuler::MaxOf(r), HorizontalRuler::MaxOf(b) };
    }

}
