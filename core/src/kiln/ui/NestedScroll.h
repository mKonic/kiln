#pragma once

// Nested scroll (spec 03 req. 7, NestedScrollModifier/NestedScrollNode): a scrollable offers a
// delta to its ancestors before consuming (pre-scroll, outermost first), consumes, then offers
// the rest after (post-scroll, nearest first). Flings go through the same chain.

#include "kiln/runtime/Task.h"
#include "kiln/ui/Modifier.h"

namespace kiln::ui {

    enum class NestedScrollSource : std::uint8_t { UserInput, SideEffect };

    // The host view's nested-scrolling parent (NestedScrollingChildHelper on the View side):
    // View pixels and signs. axes: 1 horizontal, 2 vertical; touch: TYPE_TOUCH, else NON_TOUCH.
    class HostNestedScrollParent {
    public:
        virtual ~HostNestedScrollParent() = default;
        virtual bool Start(int axes, bool touch) = 0;
        virtual void PreScroll(int dx, int dy, bool touch, int consumed[2]) = 0;
        virtual void Scroll(int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed, bool touch, int consumed[2]) = 0;
        virtual bool PreFling(float vx, float vy) = 0;
        virtual bool Fling(float vx, float vy, bool consumed) = 0;
        virtual void StopAll() = 0;
    };

    class NestedScrollConnection {
    public:
        virtual ~NestedScrollConnection() = default;
        virtual Offset OnPreScroll(Offset, NestedScrollSource) { return {}; }
        virtual Offset OnPostScroll(Offset /*consumed*/, Offset /*available*/, NestedScrollSource) { return {}; }
        // Flings suspend (onPreFling / onPostFling): a connection may animate before answering
        // with the velocity it consumed.
        virtual rt::Task<Offset> OnPreFling(Offset /*available*/) { co_return Offset{}; }
        virtual rt::Task<Offset> OnPostFling(Offset /*consumed*/, Offset /*available*/) { co_return Offset{}; }
    };

    // A nested-scroll node in a modifier chain. Its connection (may be null) participates when a
    // descendant dispatches; Dispatch* sends this node's own scroll to its ancestors.
    class NestedScrollNode : public ModifierNode {
    public:
        NestedScrollConnection* connection = nullptr;
        NestedScrollNode* AsNestedScroll() override { return this; }

        Offset DispatchPreScroll(Offset available, NestedScrollSource s) { auto* p = Parent(); return p ? p->PreScroll(available, s) : Offset{}; }
        Offset DispatchPostScroll(Offset consumed, Offset available, NestedScrollSource s) {
            auto* p = Parent(); return p ? p->PostScroll(consumed, available, s) : Offset{};
        }
        // The ancestors' fling, before (outermost first) and after (nearest first) this node's
        // own. Ancestors detached while an earlier one runs are skipped.
        rt::Task<Offset> DispatchPreFling(Offset v);
        rt::Task<Offset> DispatchPostFling(Offset consumed, Offset available);
        NestedScrollNode* NestedParent() const { return Parent(); }

    private:
        NestedScrollNode* Parent() const;
        Offset PreScroll(Offset available, NestedScrollSource s) {
            const Offset parent = DispatchPreScroll(available, s);
            const Offset own = connection ? connection->OnPreScroll(available - parent, s) : Offset{};
            return parent + own;
        }
        Offset PostScroll(Offset consumed, Offset available, NestedScrollSource s) {
            const Offset own = connection ? connection->OnPostScroll(consumed, available, s) : Offset{};
            return own + DispatchPostScroll(consumed + own, available - own, s);
        }
    };

}
