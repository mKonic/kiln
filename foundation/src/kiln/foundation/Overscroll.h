#pragma once

// Stretch overscroll (AndroidEdgeEffectOverscrollEffect + StretchOverscrollNode): scroll deltas a
// scrollable could not use pull edge effects; the opposite scroll relaxes them first; flings
// absorb into them; the content draws through HWUI's stretch distortion.

#include "kiln/foundation/EdgeEffect.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/NestedScroll.h"

#include <functional>
#include <memory>

namespace kiln::foundation {

    using ui::Offset;
    using ui::Size;
    using ui::NestedScrollSource;

    class OverscrollEffect {
    public:
        // applyToScroll: returns what was consumed in total (stretch + performScroll).
        Offset ApplyToScroll(Offset delta, NestedScrollSource source, const std::function<Offset(Offset)>& performScroll);
        // Before the fling: the stretch may absorb the velocity (returns what it took).
        Offset ConsumePreFling(Offset velocity);
        // After the fling: leftover velocity is absorbed into the edges.
        void AbsorbPostFling(Offset leftover);
        bool IsInProgress() const;

        // For the draw node.
        void UpdateSize(Size s);
        // Advances recede springs; the stretch vector (x, y) for the content; true while animating.
        bool Advance(std::int64_t nowMillis, float& stretchX, float& stretchY);
        int RedrawSignal() const { return m_Redraw.Get(); }
        void Invalidate() { m_Redraw.Set(m_Redraw.Peek() + 1); }
        float density = 1;
        // Millisecond clock for pulls and releases (AnimationUtils.currentAnimationTimeMillis);
        // tests replace it.
        std::function<std::int64_t()> clock;

    private:
        float PullTop(float dy), PullBottom(float dy), PullLeft(float dx), PullRight(float dx);
        void ReleaseAll();
        std::int64_t Now() const;
        bool Stretched(const EdgeEffect& e) const { return e.Distance() != 0; }
        EdgeEffect m_Top, m_Bottom, m_Left, m_Right;
        Size m_Size;
        bool m_Cycle = false;
        rt::State<int> m_Redraw{ 0 };
    };

    // The overscroll draw node; also tells the effect its container size.
    ui::Modifier Overscroll(ui::Modifier m, std::shared_ptr<OverscrollEffect> effect);
    std::shared_ptr<OverscrollEffect> RememberOverscrollEffect(rt::Key key = {});

}
