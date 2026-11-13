#pragma once

// SubcomposeLayout and BoxWithConstraints: content composed during measure, so it can depend on
// the incoming constraints or on other children's sizes. Sub-compositions see the Locals of the
// place SubcomposeLayout was composed.

#include "kiln/foundation/Layouts.h"
#include <kiln/Function.h>
#include "kiln/runtime/Composition.h"
#include "kiln/ui/LayoutNode.h"

#include <algorithm>
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
        virtual std::vector<Measurable*> Subcompose(rt::Key slotId, kiln::Function<void()> content) = 0;
        // Content kept by identity, as Compose compares a slot's content lambda: the slot
        // recomposes only when `content` is another object or something it read changed. State
        // it reads (set before this call) stands in for arguments that change.
        virtual std::vector<Measurable*> Subcompose(rt::Key slotId, const std::shared_ptr<const kiln::Function<void()>>& content) = 0;
    };

    using SubcomposeMeasure = kiln::Function<MeasureResult(SubcomposeMeasureScope&, const Constraints&)>;

    void SubcomposeLayout(const Modifier& modifier, SubcomposeMeasure measure, rt::Key key = {});

    // SubcomposeSlotReusePolicy: which slots that leave are kept for reuse, and which kept slot
    // a new slot id may take over. kiln composes a slot anew rather than reusing another's
    // nodes, so a slot that leaves is disposed: its state is forgotten either way, as Compose
    // forgets a retained slot's state.
    class SubcomposeSlotReusePolicy {
    public:
        class SlotIdsSet {
        public:
            void Add(rt::Key id) { if (!Contains(id)) m_Ids.push_back(id); }
            bool Remove(rt::Key id) { return std::erase(m_Ids, id) > 0; }
            bool Contains(rt::Key id) const { return std::find(m_Ids.begin(), m_Ids.end(), id) != m_Ids.end(); }
            void Clear() { m_Ids.clear(); }
            std::size_t Size() const { return m_Ids.size(); }
            bool IsEmpty() const { return m_Ids.empty(); }
            void RemoveIf(const kiln::Function<bool(const rt::Key&)>& pred) { std::erase_if(m_Ids, pred); }
            void RetainIf(const kiln::Function<bool(const rt::Key&)>& pred) { std::erase_if(m_Ids, [&](const rt::Key& k) { return !pred(k); }); }
            const std::vector<rt::Key>& Ids() const { return m_Ids; }
        private:
            std::vector<rt::Key> m_Ids;
        };
        virtual ~SubcomposeSlotReusePolicy() = default;
        // getSlotsToRetain: removes from `slotIds` the slots not to keep.
        virtual void GetSlotsToRetain(SlotIdsSet& slotIds) = 0;
        virtual bool AreCompatible(rt::Key slotId, rt::Key reusableSlotId) = 0;
    };
    // SubcomposeSlotReusePolicy(maxSlotsToRetainForReuse): keeps the most recent slots, any compatible.
    std::shared_ptr<SubcomposeSlotReusePolicy> MakeSubcomposeSlotReusePolicy(int maxSlotsToRetainForReuse);

    // SubcomposeLayoutState: shared with SubcomposeLayout to compose slots ahead of measure.
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

        // PausedPrecomposition: a precomposition run in steps (resume until complete, then apply).
        // kiln composes in one step: the first resume completes it.
        class PausedPrecomposition {
        public:
            bool IsComplete() const { return m_Complete; }
            // Composes; returns whether it is complete.
            bool Resume(const rt::ShouldPauseCallback& shouldPause);
            // The handle of the composed slot (composing it first when not complete).
            PrecomposedSlotHandle Apply();
            void Cancel();
        private:
            friend class SubcomposeLayoutState;
            SubcomposeLayoutState* m_Owner = nullptr;
            rt::Key m_Slot = rt::Key::Positional();
            kiln::Function<void()> m_Content;
            PrecomposedSlotHandle m_Handle;
            bool m_Complete = false;
            bool m_Cancelled = false;
        };

        SubcomposeLayoutState();
        explicit SubcomposeLayoutState(std::shared_ptr<SubcomposeSlotReusePolicy> slotReusePolicy);
        // Composes `content` for `slotId` now (nothing when the layout is not composed yet).
        PrecomposedSlotHandle Precompose(rt::Key slotId, kiln::Function<void()> content);
        // createPausedPrecomposition
        PausedPrecomposition CreatePausedPrecomposition(rt::Key slotId, kiln::Function<void()> content);
        const std::shared_ptr<SubcomposeSlotReusePolicy>& SlotReusePolicy() const { return m_Policy; }
        std::shared_ptr<void> Impl() const { return m_Impl; }

    private:
        std::shared_ptr<void> m_Impl;
        std::shared_ptr<SubcomposeSlotReusePolicy> m_Policy;
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
                            kiln::Function<void(const BoxWithConstraintsScope&)> content, rt::Key key = {});
    inline void BoxWithConstraints(const Modifier& modifier, kiln::Function<void(const BoxWithConstraintsScope&)> content) {
        BoxWithConstraints(modifier, Alignment::TopStart(), false, std::move(content));
    }

}
