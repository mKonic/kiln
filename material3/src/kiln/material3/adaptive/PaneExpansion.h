#pragma once

// adaptive-layout PaneExpansionState.kt and DragToResizeState.kt.
//
// PaneExpansionState: how a two-pane layout splits its width: a first pane width or proportion,
// or where the user dragged the drag handle to; anchors the handle settles to after a drag
// (after flinging), and their descriptions for accessibility. One state per expansion key (the
// pair of expanded panes), remembered across configuration changes.
//
// DragToResizeState: the size of a levitated pane resized by dragging its free edge (or by
// clicking to expand, collapse or restore it).

#include "kiln/animation/Spec.h"
#include "kiln/foundation/Draggable.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/material3/adaptive/PaneScaffoldValue.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Geometry.h"

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::material3::adaptive {

    // PaneExpansionAnchor: a proportion of the width, or an offset from the start or end edge.
    struct PaneExpansionAnchor {
        enum class Type : std::uint8_t { Unspecified = 0, Proportion = 1, OffsetFromStart = 2, OffsetFromEnd = 3 };
        Type type = Type::Proportion;
        float proportion = 0;   // Proportion
        ui::Dp offset{ 0 };     // the offsets

        static PaneExpansionAnchor Proportion(float proportion) { return { Type::Proportion, proportion, ui::Dp(0) }; }
        static PaneExpansionAnchor OffsetFromStart(ui::Dp offset) { return { Type::OffsetFromStart, 0, offset }; }
        static PaneExpansionAnchor OffsetFromEnd(ui::Dp offset) { return { Type::OffsetFromEnd, 0, offset }; }

        // The handle's position for this anchor in a width of `totalSizePx` (mirrored in RTL).
        int PositionIn(int totalSizePx, const ui::Density& density, std::optional<ui::LayoutDirection> direction) const;
        // "50 percent", "100 dp from start", ... (for the drag handle's semantics), in `locales`.
        std::string Description(const std::vector<std::string>& locales) const;
        bool operator==(const PaneExpansionAnchor& o) const {
            if (type != o.type) return false;
            return type == Type::Proportion ? proportion == o.proportion : offset == o.offset;
        }
    };

    class PaneExpansionState {
    public:
        static constexpr int Unspecified = -1;
        // PaneExpansionStateData: what is remembered per key.
        struct Data {
            rt::State<int> firstPaneWidth{ Unspecified };
            rt::State<float> firstPaneProportion{ NAN };
            rt::State<int> currentDraggingOffset{ Unspecified };
            rt::State<std::optional<PaneExpansionAnchor>> currentAnchor{ std::nullopt };
        };

        explicit PaneExpansionState(std::shared_ptr<Data> data = std::make_shared<Data>(),
                                    std::function<float(float)> consumeDragDelta = nullptr);

        // The anchor the expansion is at (after settling or AnimateTo), if any.
        std::optional<PaneExpansionAnchor> CurrentAnchor() const { return m_Data.Get()->currentAnchor.Get(); }
        // None of the first pane width, proportion or dragging offset is set.
        bool IsUnspecified() const;
        void SetFirstPaneWidth(int firstPaneWidth);
        void SetFirstPaneProportion(float firstPaneProportion);
        // Animates the handle to one of the anchors.
        rt::Task<> AnimateTo(PaneExpansionAnchor anchor, float initialVelocity = 0);
        void Clear();

        // internal (ThreePaneScaffold and the drag handle)
        int FirstPaneWidth() const;
        float FirstPaneProportion() const { return m_Data.Get()->firstPaneProportion.Get(); }
        int CurrentDraggingOffset() const { return m_Data.Get()->currentDraggingOffset.Get(); }
        std::optional<PaneExpansionAnchor> NextAnchor() const;
        bool IsDragging() const { return m_Draggable.IsDragging(); }
        bool IsSettling() const { return m_Settling.Get(); }
        bool IsDraggingOrSettling() const { return IsDragging() || IsSettling(); }
        foundation::DraggableState& DraggableState() { return m_Draggable; }
        void Restore(std::shared_ptr<Data> data, std::vector<PaneExpansionAnchor> anchors, anim::SpecPtr anchoringAnimationSpec,
                     foundation::FlingBehavior flingBehavior, std::optional<PaneExpansionAnchor> initialAnchorForCurrentAnchors);
        void OnMeasured(int measuredWidth, const ui::Density& density, ui::LayoutDirection direction);
        void OnExpansionOffsetMeasured(int measuredOffset) { m_CurrentMeasuredDraggingOffset = measuredOffset; }
        void SnapToAnchor(const PaneExpansionAnchor& anchor);
        // After a drag: fling, then settle at the closest anchor (in the fling's direction).
        rt::Task<> SettleToAnchorIfNeeded(float velocity);
        // Launched from callbacks (a drag's end, an accessibility action); a newer one replaces it.
        void LaunchSettle(float velocity) { m_Job = rt::Launch(rt::Dispatcher::Main(), SettleToAnchorIfNeeded(velocity)); }
        void LaunchAnimateTo(PaneExpansionAnchor anchor) { m_Job = rt::Launch(rt::Dispatcher::Main(), AnimateTo(anchor)); }
        int MaxExpansionWidth() const { return m_MaxExpansionWidth.Get(); }
        int CurrentMeasuredDraggingOffset() const { return m_CurrentMeasuredDraggingOffset; }
        std::shared_ptr<Data> GetData() const { return m_Data.Peek(); }

        static anim::SpecPtr DefaultAnchoringAnimationSpec();   // spring(0.8, 380, threshold 1)

    private:
        void SetCurrentDraggingOffset(int value);
        void SetCurrentAnchor(std::optional<PaneExpansionAnchor> a) { m_Data.Peek()->currentAnchor.Set(std::move(a)); }
        void UpdateAnchorPositions();
        rt::Task<> AnimateToInternal(int offset, float initialVelocity, std::uint64_t token);
        float ScrollBy(float pixels);

        struct IndexedAnchorPosition { int position, index; };
        rt::State<std::shared_ptr<Data>> m_Data;
        std::function<float(float)> m_ConsumeDragDelta;
        foundation::DraggableState m_Draggable;
        rt::State<bool> m_Settling{ false };
        rt::State<int> m_MaxExpansionWidth{ Unspecified };
        int m_CurrentMeasuredDraggingOffset = Unspecified;
        rt::State<std::vector<PaneExpansionAnchor>> m_Anchors{ {} };
        std::vector<IndexedAnchorPosition> m_AnchorPositions;   // sorted by position
        anim::SpecPtr m_AnchoringSpec = DefaultAnchoringAnimationSpec();
        foundation::FlingBehavior m_Fling;
        std::optional<ui::Density> m_MeasuredDensity;
        std::optional<ui::LayoutDirection> m_MeasuredDirection;
        std::uint64_t m_Mutation = 0;   // the running settle or animation (a newer one stops it)
        rt::Job m_Job;
    };

    struct PaneExpansionStateOptions {
        std::vector<PaneExpansionAnchor> anchors;
        int initialAnchoredIndex = -1;
        anim::SpecPtr anchoringAnimationSpec;           // null: the default spring
        foundation::FlingBehavior flingBehavior;        // null: the platform spline decay
        std::function<float(float)> consumeDragDelta;  // null: consumes nothing
    };
    // rememberPaneExpansionState(key / keyProvider, ...): the state of the expansion for `key`
    // (each key keeps its own, saved across configuration changes).
    PaneExpansionState& RememberPaneExpansionState(PaneExpansionStateKey key = {}, const PaneExpansionStateOptions& options = {}, rt::Key callKey = {});
    inline PaneExpansionState& RememberPaneExpansionState(const ThreePaneScaffoldValue& keyProvider, const PaneExpansionStateOptions& options = {},
                                                          rt::Key callKey = {}) {
        return RememberPaneExpansionState(keyProvider.ExpansionStateKey(), options, callKey);
    }
    // rememberDefaultPaneExpansionState: a remembered state for the key when mutable (a drag
    // handle is shown), else a stub.
    PaneExpansionState& RememberDefaultPaneExpansionState(std::function<PaneExpansionStateKey()> keyProvider, bool mutableState, rt::Key callKey = {});

    // --- DragToResizeState ----------------------------------------------------------------------

    enum class DockedEdge : std::uint8_t { Top, Bottom, Start, End };

    class DragToResizeState {
    public:
        enum class State : std::uint8_t { Expanded, Collapsed, Dragged, Default };
        // Left / Right (horizontal, a pane docked at the start or end) or Top / Bottom.
        enum class Kind : std::uint8_t { Left, Right, Top, Bottom };
        explicit DragToResizeState(Kind kind) : m_Kind(kind) {}

        bool IsHorizontal() const { return m_Kind == Kind::Left || m_Kind == Kind::Right; }
        foundation::Orientation Orientation() const { return IsHorizontal() ? foundation::Orientation::Horizontal : foundation::Orientation::Vertical; }
        State CurrentState() const { return m_State.Get(); }
        // dispatchRawDelta: drags the free edge (the edge away from the docked one).
        void DispatchRawDelta(float delta);
        foundation::DraggableState& DraggableState() { return m_Draggable; }
        // animateTo(state): expands, collapses or restores the size.
        void AnimateTo(State target);
        State NextState() const;
        // The levitated pane's size along its axis (the other axis keeps the measuring size).
        int GetDraggedWidth(int measuringWidth, int defaultMinWidth, int scaffoldWidth);
        int GetDraggedHeight(int measuringHeight, int defaultMinHeight, int scaffoldHeight);

        // internal (rememberDragToResizeState)
        int minSize = 0, maxSize = 0;
        std::shared_ptr<void> scope;   // the composition's coroutine scope for animations

    private:
        float Size() const { return m_Size.Get(); }
        void SetSize(float value);
        int GetAndUpdateDraggedSize(int measuringSize);
        float TargetSize(State s) const;
        float RangeStart() const { return IsHorizontal() ? m_WidthRange.first : m_HeightRange.first; }
        float RangeEnd() const { return IsHorizontal() ? m_WidthRange.second : m_HeightRange.second; }
        float ConvertDelta(float delta) const { return m_Kind == Kind::Right || m_Kind == Kind::Bottom ? -delta : delta; }

        Kind m_Kind;
        rt::State<float> m_Size{ NAN };
        rt::State<State> m_State{ State::Default };
        std::pair<float, float> m_WidthRange{ 0, 0 }, m_HeightRange{ 0, 0 };
        float m_LastMeasuredSize = NAN, m_LastDraggedSize = NAN;
        foundation::DraggableState m_Draggable{ [this](float d) { DispatchRawDelta(d); } };
        rt::Job m_Animation;
    };

    // rememberDragToResizeState(dockedEdge, minSize, maxSize): unset sizes take the defaults
    // (a 48 dp minimum, the scaffold's size at most).
    std::shared_ptr<DragToResizeState> RememberDragToResizeState(DockedEdge dockedEdge, std::optional<ui::Dp> minSize = std::nullopt,
                                                                 std::optional<ui::Dp> maxSize = std::nullopt, rt::Key key = {});

    // Modifier.dragToResize(state, showIndication): drags the pane's free edge; a click expands,
    // collapses or restores it.
    ui::Modifier DragToResize(ui::Modifier m, std::shared_ptr<DragToResizeState> state, bool showIndication = true);

}
