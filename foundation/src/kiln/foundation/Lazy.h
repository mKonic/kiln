#pragma once

// Lazy lists (spec 05 §Lazy layouts): items are composed during measure, only those in (or
// entering) the viewport. Each item is a slot: a holder node with its own composition. The scroll
// position is the first visible item (tracked by key across data changes) and its offset.
// Not yet: reuse pool by contentType, saveable item state, prefetch, item animations, sticky
// headers, beyond-bounds composition for focus (PLAN M5).

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Scroll.h"

#include <map>

namespace kiln::foundation {

    class LazyListState final : public ScrollableState {
    public:
        explicit LazyListState(int firstIndex = 0, int firstOffset = 0) : m_Index(firstIndex), m_Offset(firstOffset) {}
        int FirstVisibleItemIndex() const { return m_Index.Get(); }
        int FirstVisibleItemScrollOffset() const { return m_Offset.Get(); }
        float ConsumeScrollDelta(float delta) override;
        bool CanScrollForward() const override { return m_CanForward; }
        bool CanScrollBackward() const override { return m_Index.Peek() > 0 || m_Offset.Peek() > 0; }
        void ScrollToItem(int index, int offset = 0);

    private:
        friend struct LazyCore;
        rt::State<int> m_Index, m_Offset;
        float m_PendingScroll = 0;
        float m_LastConsumed = 0;
        bool m_CanForward = true;
        rt::Key m_FirstKey = rt::Key::Positional();
        bool m_HasFirstKey = false;
        LayoutNode* m_Node = nullptr;   // the list, for forced remeasure
    };

    class LazyListScope {
    public:
        void Item(std::function<void()> content, rt::Key key = rt::Key::Positional());
        void Items(int count, std::function<void(int)> content, std::function<rt::Key(int)> key = {});

        struct Interval { int count; std::function<void(int)> content; std::function<rt::Key(int)> key; };
        std::vector<Interval> intervals;
    };

    struct LazyListOptions {
        PaddingValues contentPadding;
        Dp spacing{ 0 };
        AlignmentH horizontalAlignment = AlignmentH::Start();   // LazyColumn
        AlignmentV verticalAlignment = AlignmentV::Top();       // LazyRow
        bool userScrollEnabled = true;
    };

    void LazyColumn(const Modifier& modifier, LazyListState& state, std::function<void(LazyListScope&)> content,
                    LazyListOptions options = {}, rt::Key key = {});
    void LazyRow(const Modifier& modifier, LazyListState& state, std::function<void(LazyListScope&)> content,
                 LazyListOptions options = {}, rt::Key key = {});

}
