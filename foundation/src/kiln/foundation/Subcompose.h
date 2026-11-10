#pragma once

// SubcomposeLayout and BoxWithConstraints: content composed during measure, so it can depend on
// the incoming constraints or on other children's sizes. Sub-compositions see the Locals of the
// place SubcomposeLayout was composed.

#include "kiln/foundation/Layouts.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/LayoutNode.h"

#include <functional>
#include <vector>

namespace kiln::foundation {

    using ui::Constraints;
    using ui::Measurable;
    using ui::MeasureResult;
    using ui::MeasureScope;

    class SubcomposeMeasureScope : public MeasureScope {
    public:
        explicit SubcomposeMeasureScope(const MeasureScope& s) : MeasureScope(s) {}
        // Composes `content` for `slotId` (once per measure) and returns its root layouts.
        virtual std::vector<Measurable*> Subcompose(rt::Key slotId, std::function<void()> content) = 0;
        // Content kept by identity, as Compose compares a slot's content lambda: the slot
        // recomposes only when `content` is another object or something it read changed. State
        // it reads (set before this call) stands in for arguments that change.
        virtual std::vector<Measurable*> Subcompose(rt::Key slotId, const std::shared_ptr<const std::function<void()>>& content) = 0;
    };

    using SubcomposeMeasure = std::function<MeasureResult(SubcomposeMeasureScope&, const Constraints&)>;

    void SubcomposeLayout(const Modifier& modifier, SubcomposeMeasure measure, rt::Key key = {});

    // SubcomposeLayoutState: shared with SubcomposeLayout to compose slots ahead of measure.
    // (Compose's slot reuse policy needs reusable compositions in the runtime; slots that leave
    // are disposed.)
    class SubcomposeLayoutState {
    public:
        // PrecomposedSlotHandle: the slot stays composed (unplaced) until a measure subcomposes
        // the same id, or until Dispose.
        class PrecomposedSlotHandle {
        public:
            PrecomposedSlotHandle() = default;
            void Dispose();
            int PlaceablesCount() const;
            // Measures the slot's `index`-th root ahead of time.
            void Premeasure(int index, const Constraints& constraints);
        private:
            friend class SubcomposeLayoutState;
            std::weak_ptr<void> m_State;
            std::uint64_t m_Slot = 0;
        };

        SubcomposeLayoutState();
        // Composes `content` for `slotId` now (nothing when the layout is not composed yet).
        PrecomposedSlotHandle Precompose(rt::Key slotId, std::function<void()> content);
        std::shared_ptr<void> Impl() const { return m_Impl; }

    private:
        std::shared_ptr<void> m_Impl;
    };

    void SubcomposeLayout(SubcomposeLayoutState& state, const Modifier& modifier, SubcomposeMeasure measure);

    struct BoxWithConstraintsScope {
        Constraints constraints;
        ui::Density density;
        Dp MinWidth() const { return Dp(float(constraints.minWidth) / density.density); }
        Dp MaxWidth() const { return constraints.HasBoundedWidth() ? Dp(float(constraints.maxWidth) / density.density) : Dp(std::numeric_limits<float>::infinity()); }
        Dp MinHeight() const { return Dp(float(constraints.minHeight) / density.density); }
        Dp MaxHeight() const { return constraints.HasBoundedHeight() ? Dp(float(constraints.maxHeight) / density.density) : Dp(std::numeric_limits<float>::infinity()); }
    };

    void BoxWithConstraints(const Modifier& modifier, Alignment contentAlignment, bool propagateMinConstraints,
                            std::function<void(const BoxWithConstraintsScope&)> content, rt::Key key = {});
    inline void BoxWithConstraints(const Modifier& modifier, std::function<void(const BoxWithConstraintsScope&)> content) {
        BoxWithConstraints(modifier, Alignment::TopStart(), false, std::move(content));
    }

}
