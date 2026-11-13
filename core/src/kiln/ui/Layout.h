#pragma once

#include <algorithm>
#include <kiln/Function.h>
#include <climits>

// The measure/layout contract (spec 03 §layout, req. 2): constraints down, size up, each child
// measured at most once per pass, placement in a separate phase, placeRelative mirrors in RTL.

#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kiln::ui {

    class PlacementScope;
    class WindowInsetsHolder;
    class LayoutNode;

    class ModifierNode;

    // LayoutCoordinates: a space of a node. One of its coordinators (0: its outer bounds, the
    // last: the space its children are placed in), in the main layout or in the lookahead one
    // (LookaheadLayoutCoordinates: where the lookahead pass put it); or the space a modifier sits
    // in (through the layers before it), as onGloballyPositioned hands it over. A view read when
    // asked: a stored copy follows later moves. It does not keep the node alive; with an alive
    // token it notices the node is gone.
    struct LayoutCoordinates {
        LayoutCoordinates() = default;
        LayoutCoordinates(const LayoutNode* node, std::size_t coordinator, bool lookahead = false)
            : node(node), coordinator(coordinator), lookahead(lookahead) {}
        LayoutCoordinates(const LayoutNode* node, const ModifierNode* at, std::weak_ptr<const char> alive)
            : node(node), at(at), alive(std::move(alive)), tracked(true) {}

        const LayoutNode* node = nullptr;
        std::size_t coordinator = 0;
        bool lookahead = false;
        const ModifierNode* at = nullptr;   // a modifier's space instead of a coordinator's
        std::weak_ptr<const char> alive;
        bool tracked = false;

        // The node is there and placed.
        bool IsAttached() const;
        IntSize Size() const;
        IntSize GetSize() const { return IsAttached() ? Size() : IntSize{}; }
        Offset PositionInRoot() const;
        Rect BoundsInRoot() const;
        // positionInParent / parentLayoutCoordinates.size: in the parent layout's content space
        // (where it places its children; the root's own position and the window without one).
        Offset PositionInParent() const;
        IntSize ParentSize() const;
        // localPositionOf: `relativeToSource` in `source`'s space, in this one. Main coordinates
        // map through every layer; lookahead ones add up lookahead offsets, and cross into main
        // space at their lookahead root's parent (as LookaheadLayoutCoordinates does).
        Offset LocalPositionOf(const LayoutCoordinates& source, Offset relativeToSource = {}) const;
        // includeMotionFrameOfReference false: without offsets placed under the motion frame of
        // reference (scrolling), the structural position only.
        Offset LocalPositionOf(const LayoutCoordinates& source, Offset relativeToSource, bool includeMotionFrameOfReference) const;
        // localBoundingBoxOf: `source`'s bounds (its corners mapped) in this space.
        Rect LocalBoundingBoxOf(const LayoutCoordinates& source) const;
        // positionInWindow / boundsInWindow: the root sits at the window's origin, so these are
        // the root ones (bounds kept inside the window).
        Offset PositionInWindow() const { return PositionInRoot(); }
        Rect BoundsInWindow() const;
        // boundsInParent: the bounding box in the parent layout's content space.
        Rect BoundsInParent() const;
        // findRootCoordinates: the window root's.
        LayoutCoordinates FindRootCoordinates() const;
        bool operator==(const LayoutCoordinates& o) const {
            return node == o.node && coordinator == o.coordinator && lookahead == o.lookahead && at == o.at;
        }
    };

    // Ruler (Ruler.kt): a line an ancestor's layout provides to its whole subtree, read during
    // placement. Vertical rulers are x positions, horizontal ones y positions. A ruler is its
    // identity: copies share it. Derived rulers compute their value from others.
    struct RulerData {
        bool vertical = true;
        kiln::Function<float(PlacementScope&, float defaultValue)> calculate;   // derived rulers
    };
    class Ruler {
    public:
        bool IsVertical() const { return m_Data->vertical; }
        const RulerData* Id() const { return m_Data.get(); }
        bool operator==(const Ruler& o) const { return m_Data == o.m_Data; }
    protected:
        explicit Ruler(std::shared_ptr<const RulerData> d) : m_Data(std::move(d)) {}
        std::shared_ptr<const RulerData> m_Data;
    };
    class VerticalRuler final : public Ruler {
    public:
        VerticalRuler() : Ruler(std::make_shared<RulerData>(RulerData{ true, {} })) {}
        // The largest / smallest of the rulers that have a value (default when none has).
        static VerticalRuler MaxOf(std::vector<VerticalRuler> rulers);
        static VerticalRuler MinOf(std::vector<VerticalRuler> rulers);
        static VerticalRuler Derived(kiln::Function<float(PlacementScope&, float defaultValue)> calculation) {
            return VerticalRuler(std::make_shared<RulerData>(RulerData{ true, std::move(calculation) }));
        }
    private:
        using Ruler::Ruler;
    };
    class HorizontalRuler final : public Ruler {
    public:
        HorizontalRuler() : Ruler(std::make_shared<RulerData>(RulerData{ false, {} })) {}
        static HorizontalRuler MaxOf(std::vector<HorizontalRuler> rulers);
        static HorizontalRuler MinOf(std::vector<HorizontalRuler> rulers);
        static HorizontalRuler Derived(kiln::Function<float(PlacementScope&, float defaultValue)> calculation) {
            return HorizontalRuler(std::make_shared<RulerData>(RulerData{ false, std::move(calculation) }));
        }
    private:
        using Ruler::Ruler;
    };
    // RectRulers: four rulers bounding an area (insets, safe areas).
    struct RectRulers {
        VerticalRuler left;
        HorizontalRuler top;
        VerticalRuler right;
        HorizontalRuler bottom;
        // innermostOf: the largest left/top and smallest right/bottom; outermostOf the reverse.
        static RectRulers InnermostOf(const std::vector<RectRulers>& rulers);
        static RectRulers OutermostOf(const std::vector<RectRulers>& rulers);
    };

    // RulerScope: where a layout provides ruler values, in its own coordinates.
    class RulerScope : public Density {
    public:
        RulerScope(Density d, LayoutDirection dir, int width, kiln::Function<Offset(Offset)> toRoot)
            : Density(d), m_Dir(dir), m_Width(width), m_ToRoot(std::move(toRoot)) {}
        void Provides(const Ruler& ruler, float value) { Set(ruler.Id(), value); }
        // providesRelative: from the start edge (mirrored in RTL).
        void ProvidesRelative(const VerticalRuler& ruler, float value) { Set(ruler.Id(), m_Dir == LayoutDirection::Rtl ? float(m_Width) - value : value); }
        // coordinates.localToRoot, for values that depend on where the layout is.
        Offset LocalToRoot(Offset p) const { return m_ToRoot ? m_ToRoot(p) : p; }
        const std::vector<std::pair<const RulerData*, float>>& Values() const { return m_Values; }
    private:
        void Set(const RulerData* id, float v) {
            for (auto& [r, value] : m_Values) if (r == id) { value = v; return; }
            m_Values.emplace_back(id, v);
        }
        LayoutDirection m_Dir;
        int m_Width;
        kiln::Function<Offset(Offset)> m_ToRoot;
        std::vector<std::pair<const RulerData*, float>> m_Values;
    };

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
        // Placed inside withMotionFrameOfReferencePlacement (a scroll offset, say).
        virtual void SetPlacedUnderMotionFrameOfReference(bool) {}

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
            p.SetPlacedUnderMotionFrameOfReference(m_MotionFrame);
            p.PlaceAt(IntOffset{ x, y } + p.ApparentToRealOffset(), zIndex);
        }
        // withMotionFrameOfReferencePlacement: placements in `block` are motion (scrolling)
        // rather than structure; coordinates can leave them out (LocalPositionOf with
        // includeMotionFrameOfReference false).
        void WithMotionFrameOfReferencePlacement(const kiln::Function<void()>& block) {
            const bool was = m_MotionFrame;
            m_MotionFrame = true;
            block();
            m_MotionFrame = was;
        }
        // Mirrors x in RTL: parentWidth - width - x (Placeable.placeRelative).
        void PlaceRelative(Placeable& p, int x, int y, float zIndex = 0) {
            if (m_Dir == LayoutDirection::Rtl) x = m_ParentWidth - p.Width() - x;
            Place(p, x, y, zIndex);
        }
        LayoutDirection Direction() const { return m_Dir; }
        int ParentWidth() const { return m_ParentWidth; }
        // Ruler.current: the ruler's value from the nearest layout providing it, in this
        // layout's coordinates, else the default.
        float Current(const Ruler& ruler, float defaultValue) {
            if (ruler.Id()->calculate) return ruler.Id()->calculate(*this, defaultValue);
            return m_Lookup ? m_Lookup(ruler, defaultValue) : defaultValue;
        }
        void SetRulerLookup(kiln::Function<float(const Ruler&, float)> lookup) { m_Lookup = std::move(lookup); }
        // The window's insets (for WindowInsetsRulers.getAnimation), when attached.
        const WindowInsetsHolder* Insets() const { return m_Insets; }
        void SetInsets(const WindowInsetsHolder* h) { m_Insets = h; }
        // PlacementScope.coordinates: the layout placing (its lookahead coordinates in the
        // lookahead pass); null outside a node's placement.
        const LayoutCoordinates* Coordinates() const { m_CoordinatesRead = true; return m_Coords ? &*m_Coords : nullptr; }
        void SetCoordinates(LayoutCoordinates c) { m_Coords = c; }
        // Whether the placement read Coordinates (it then reruns when the layout moves).
        bool CoordinatesRead() const { return m_CoordinatesRead; }
        bool IsLookingAhead() const { return m_Coords && m_Coords->lookahead; }

    private:
        LayoutDirection m_Dir;
        int m_ParentWidth;
        kiln::Function<float(const Ruler&, float)> m_Lookup;
        const WindowInsetsHolder* m_Insets = nullptr;
        std::optional<LayoutCoordinates> m_Coords;
        mutable bool m_CoordinatesRead = false;
        bool m_MotionFrame = false;
    };

    struct MeasureResult {
        int width = 0, height = 0;
        kiln::Function<void(PlacementScope&)> place;
        // Lines this layout provides itself (others are inherited from what it wraps).
        std::vector<std::pair<const AlignmentLine*, int>> alignmentLines = {};
        // Ruler values this layout provides to its subtree (run when it places).
        kiln::Function<void(RulerScope&)> rulers = {};
    };

    class MeasureScope : public Density {
    public:
        MeasureScope(Density d, LayoutDirection dir, bool lookingAhead = false) : Density(d), m_Dir(dir), m_LookingAhead(lookingAhead) {}
        LayoutDirection Direction() const { return m_Dir; }
        // isLookingAhead: this measure is the lookahead pass of a LookaheadScope (it settles the
        // destination layout; the main pass then approaches it).
        bool IsLookingAhead() const { return m_LookingAhead; }
        MeasureResult Layout(int width, int height, kiln::Function<void(PlacementScope&)> place = {},
                             std::vector<std::pair<const AlignmentLine*, int>> lines = {}) const {
            return { width, height, std::move(place), std::move(lines) };
        }
        MeasureResult Layout(int width, int height, std::vector<std::pair<const AlignmentLine*, int>> lines,
                             kiln::Function<void(RulerScope&)> rulers, kiln::Function<void(PlacementScope&)> place) const {
            return { width, height, std::move(place), std::move(lines), std::move(rulers) };
        }

    private:
        LayoutDirection m_Dir;
        bool m_LookingAhead = false;
    };

    // ApproachMeasureScope: the main pass of an approach modifier, with the lookahead pass's
    // constraints and size for it (the destination it approaches).
    class ApproachMeasureScope : public MeasureScope {
    public:
        ApproachMeasureScope(const MeasureScope& s, Constraints lookaheadConstraints, IntSize lookaheadSize)
            : MeasureScope(s), lookaheadConstraints(lookaheadConstraints), lookaheadSize(lookaheadSize) {}
        Constraints lookaheadConstraints;
        IntSize lookaheadSize;
    };

    // ApproachLayoutModifierNode: in a LookaheadScope, measures like Measure (by default straight
    // through) in the lookahead pass, and in the main pass approaches that result, typically
    // animating from where it was. While neither IsMeasurementApproachInProgress nor
    // IsPlacementApproachInProgress holds, the approach is complete: what it wraps is measured
    // with the lookahead constraints and placed at its lookahead offset. Outside a lookahead
    // scope, the node it is on becomes a lookahead root of its own.
    class ApproachLayoutModifier : public LayoutModifier {
    public:
        virtual bool IsMeasurementApproachInProgress(IntSize lookaheadSize) = 0;
        virtual bool IsPlacementApproachInProgress(PlacementScope&, const LayoutCoordinates& /*lookaheadCoordinates*/) { return false; }
        MeasureResult Measure(MeasureScope& scope, Measurable& wrapped, const Constraints& c) override;
        virtual MeasureResult ApproachMeasure(ApproachMeasureScope& scope, Measurable& wrapped, const Constraints& c) = 0;
        // The approach pass's intrinsics: through ApproachMeasure once a lookahead measure
        // happened (`scope` then carries its results), else those of what it wraps.
        virtual int MinApproachIntrinsicWidth(ApproachMeasureScope* scope, Measurable& wrapped, int height);
        virtual int MaxApproachIntrinsicWidth(ApproachMeasureScope* scope, Measurable& wrapped, int height);
        virtual int MinApproachIntrinsicHeight(ApproachMeasureScope* scope, Measurable& wrapped, int width);
        virtual int MaxApproachIntrinsicHeight(ApproachMeasureScope* scope, Measurable& wrapped, int width);
        ApproachLayoutModifier* AsApproach() override { return this; }
    };

    // LookaheadScope: the scope a LookaheadScope composable (or an approach modifier's lookahead
    // root) gives its content, for converting between lookahead and main coordinates.
    class LookaheadScope {
    public:
        virtual ~LookaheadScope() = default;
        // toLookaheadCoordinates: the same layout in lookahead space (itself when it has no
        // lookahead result).
        virtual LayoutCoordinates ToLookaheadCoordinates(const LayoutCoordinates& c) const;
        // lookaheadScopeCoordinates: the scope's own coordinates (main space), during placement.
        virtual LayoutCoordinates LookaheadScopeCoordinates(const PlacementScope& scope) const = 0;
        // localLookaheadPositionOf: both converted to lookahead space, then localPositionOf.
        Offset LocalLookaheadPositionOf(const LayoutCoordinates& self, const LayoutCoordinates& source, Offset relativeToSource = {}) const;
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
        using Fn = kiln::Function<MeasureResult(MeasureScope&, std::span<Measurable* const>, const Constraints&)>;
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
                    const kiln::Function<MeasureResult(std::span<Measurable* const>, const Constraints&)>& measure);
    }

}
