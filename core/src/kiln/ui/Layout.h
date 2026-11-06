#pragma once

#include <algorithm>
#include <climits>

// The measure/layout contract (spec 03 §layout, req. 2): constraints down, size up, each child
// measured at most once per pass, placement in a separate phase, placeRelative mirrors in RTL.

#include "kiln/ui/Modifier.h"

#include <functional>
#include <span>
#include <vector>

namespace kiln::ui {

    class PlacementScope;

    // AlignmentLine: a position a layout reports to its parent (baselines). Horizontal lines are
    // y positions, vertical ones x positions. A parent inheriting a line from several children
    // merges them: FirstBaseline takes the minimum, LastBaseline the maximum.
    struct AlignmentLine {
        enum class Merge : std::uint8_t { Min, Max };
        Merge merge;
        bool horizontal;
        static constexpr int Unspecified = INT_MIN;
        int Combine(int a, int b) const { return merge == Merge::Min ? std::min(a, b) : std::max(a, b); }
    };
    inline constexpr AlignmentLine FirstBaseline{ AlignmentLine::Merge::Min, true };
    inline constexpr AlignmentLine LastBaseline{ AlignmentLine::Merge::Max, true };

    class Placeable {
    public:
        virtual ~Placeable() = default;
        // The line's position from this placeable's top-left, or AlignmentLine::Unspecified.
        virtual int Get(const AlignmentLine&) { return AlignmentLine::Unspecified; }
        // Size as seen by the parent: the measured size coerced into the constraints it was
        // measured with (Placeable.width/height).
        int Width() const { return m_Width; }
        int Height() const { return m_Height; }
        IntSize MeasuredSize() const { return m_Measured; }
        const Constraints& MeasurementConstraints() const { return m_Constraints; }

    protected:
        friend class PlacementScope;
        void SetMeasured(IntSize measured, const Constraints& c) {
            m_Measured = measured;
            m_Constraints = c;
            m_Width = c.ConstrainWidth(measured.width);
            m_Height = c.ConstrainHeight(measured.height);
        }
        // A measured size outside the constraints is centred within them (Placeable
        // apparentToRealOffset), so `required` sizes overflow evenly.
        IntOffset ApparentToRealOffset() const {
            return { (m_Width - m_Measured.width) / 2, (m_Height - m_Measured.height) / 2 };
        }
        virtual void PlaceAt(IntOffset position, float zIndex) = 0;

    private:
        IntSize m_Measured;
        Constraints m_Constraints;
        int m_Width = 0, m_Height = 0;
    };

    class Measurable {
    public:
        virtual ~Measurable() = default;
        virtual Placeable& Measure(const Constraints& c) = 0;
        virtual int MinIntrinsicWidth(int height) = 0;
        virtual int MaxIntrinsicWidth(int height) = 0;
        virtual int MinIntrinsicHeight(int width) = 0;
        virtual int MaxIntrinsicHeight(int width) = 0;
        virtual const ParentData& GetParentData() const = 0;
    };

    class PlacementScope {
    public:
        PlacementScope(LayoutDirection dir, int parentWidth) : m_Dir(dir), m_ParentWidth(parentWidth) {}
        void Place(Placeable& p, int x, int y, float zIndex = 0) {
            p.PlaceAt(IntOffset{ x, y } + p.ApparentToRealOffset(), zIndex);
        }
        // Mirrors x in RTL: parentWidth - width - x (Placeable.placeRelative).
        void PlaceRelative(Placeable& p, int x, int y, float zIndex = 0) {
            if (m_Dir == LayoutDirection::Rtl) x = m_ParentWidth - p.Width() - x;
            Place(p, x, y, zIndex);
        }
        LayoutDirection Direction() const { return m_Dir; }
        int ParentWidth() const { return m_ParentWidth; }

    private:
        LayoutDirection m_Dir;
        int m_ParentWidth;
    };

    struct MeasureResult {
        int width = 0, height = 0;
        std::function<void(PlacementScope&)> place;
        // Lines this layout provides itself (others are inherited from what it wraps).
        std::vector<std::pair<const AlignmentLine*, int>> alignmentLines = {};
    };

    class MeasureScope : public Density {
    public:
        MeasureScope(Density d, LayoutDirection dir) : Density(d), m_Dir(dir) {}
        LayoutDirection Direction() const { return m_Dir; }
        MeasureResult Layout(int width, int height, std::function<void(PlacementScope&)> place = {},
                             std::vector<std::pair<const AlignmentLine*, int>> lines = {}) const {
            return { width, height, std::move(place), std::move(lines) };
        }

    private:
        LayoutDirection m_Dir;
    };

    // A layout's measure policy (MeasurePolicy). Intrinsics default to measuring the policy
    // with intrinsic stand-ins for the children (Compose's default Layout intrinsics).
    class MeasurePolicy {
    public:
        virtual ~MeasurePolicy() = default;
        virtual MeasureResult Measure(MeasureScope& scope, std::span<Measurable* const> children,
                                      const Constraints& c) = 0;
        virtual int MinIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> children, int height);
        virtual int MaxIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> children, int height);
        virtual int MinIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> children, int width);
        virtual int MaxIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> children, int width);
    };

    // A policy from a lambda, for one-off layouts.
    class LambdaMeasurePolicy final : public MeasurePolicy {
    public:
        using Fn = std::function<MeasureResult(MeasureScope&, std::span<Measurable* const>, const Constraints&)>;
        explicit LambdaMeasurePolicy(Fn fn) : m_Fn(std::move(fn)) {}
        MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> c, const Constraints& k) override { return m_Fn(s, c, k); }
    private:
        Fn m_Fn;
    };

    namespace intrinsics {
        enum class Kind { MinWidth, MaxWidth, MinHeight, MaxHeight };
        // Measures `measure` with children replaced by stand-ins whose size along the queried
        // axis comes from their own intrinsics (DefaultIntrinsicMeasurable / FixedSizeIntrinsicsPlaceable).
        int Measure(Kind kind, int crossSize, std::span<Measurable* const> children,
                    const std::function<MeasureResult(std::span<Measurable* const>, const Constraints&)>& measure);
    }

}
