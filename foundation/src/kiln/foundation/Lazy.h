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

    // LazyListItemInfo / LazyListLayoutInfo: offsets are from the content start (0 is the first
    // item's leading edge at scroll 0, before content padding).
    struct LazyListItemInfo {
        int index = 0;
        rt::Key key = rt::Key::Positional();   // the item's key, or its index when it has none
        int offset = 0;
        int size = 0;
        bool operator==(const LazyListItemInfo&) const = default;
    };
    struct LazyListLayoutInfo {
        std::vector<LazyListItemInfo> visibleItemsInfo;
        int viewportStartOffset = 0;   // -beforeContentPadding
        int viewportEndOffset = 0;
        int totalItemsCount = 0;
        IntSize viewportSize;
        Orientation orientation = Orientation::Vertical;
        int beforeContentPadding = 0, afterContentPadding = 0;
        int mainAxisItemSpacing = 0;
        bool operator==(const LazyListLayoutInfo&) const = default;
    };

    class LazyListState final : public ScrollableState {
    public:
        explicit LazyListState(int firstIndex = 0, int firstOffset = 0) : m_Index(firstIndex), m_Offset(firstOffset) {}
        int FirstVisibleItemIndex() const { return m_Index.Get(); }
        int FirstVisibleItemScrollOffset() const { return m_Offset.Get(); }
        float ConsumeScrollDelta(float delta) override;
        bool CanScrollForward() const override { return m_CanForward; }
        bool CanScrollBackward() const override { return m_Index.Peek() > 0 || m_Offset.Peek() > 0; }
        void ScrollToItem(int index, int offset = 0);
        // Scrolls in animated steps of up to 2500 dp (jumping to 100 items before the target when
        // further), then seeks the target once it is composed (LazyLayoutScrollScope).
        rt::Task<> AnimateScrollToItem(int index, int scrollOffset = 0);
        // Reading subscribes; updated by every measure.
        const LazyListLayoutInfo& LayoutInfo() const { return m_Info.Get(); }

    private:
        int LastVisibleIndex() const;
        int DistanceTo(int index, int targetOffset = 0) const;
        bool Visible(int index) const;
        void SnapToItem(int index, int offset);
        rt::State<LazyListLayoutInfo> m_Info{ LazyListLayoutInfo{} };
        float m_Density = 1;
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
