#pragma once

// Lazy lists and grids (spec 05 §Lazy layouts): items are composed during measure, only those in
// (or entering) the viewport. Each item is a slot: a holder node with its own composition and its
// own saveable registry (kept while it is off-screen). Both layouts measure lines: a list line
// is one item, a grid line is a row (column) of cells. The scroll position is the first visible
// item (tracked by key across data changes) and its offset.
// Not yet: reuse pool by contentType, prefetch, item animations, beyond-bounds composition for
// focus (PLAN M5).

#include "kiln/foundation/Arrangement.h"
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

    // LazyGridItemInfo / LazyGridLayoutInfo: offset is (cross, main) for horizontal grids too,
    // in x/y terms like Compose (x across for a vertical grid).
    struct LazyGridItemInfo {
        int index = 0;
        rt::Key key = rt::Key::Positional();
        IntOffset offset;
        IntSize size;
        int row = 0, column = 0;
        bool operator==(const LazyGridItemInfo&) const = default;
    };
    struct LazyGridLayoutInfo {
        std::vector<LazyGridItemInfo> visibleItemsInfo;
        int viewportStartOffset = 0;
        int viewportEndOffset = 0;
        int totalItemsCount = 0;
        IntSize viewportSize;
        Orientation orientation = Orientation::Vertical;
        int beforeContentPadding = 0, afterContentPadding = 0;
        int mainAxisItemSpacing = 0;
        bool operator==(const LazyGridLayoutInfo&) const = default;
    };

    struct LazyCore;

    // animateItem(fadeInSpec, placementSpec, fadeOutSpec): call on the root modifier of a lazy
    // item. New items fade in, moved items animate to their new place, removed items fade out
    // where they were. A null spec turns that part off. Defaults: spring(StiffnessMediumLow).
    struct LazyItemAnimationSpecs {
        anim::SpecPtr fadeIn = anim::Spring(1.0f, anim::spring::StiffnessMediumLow);
        anim::SpecPtr placement = anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 1.0f);
        anim::SpecPtr fadeOut = anim::Spring(1.0f, anim::spring::StiffnessMediumLow);
    };
    Modifier AnimateItem(Modifier m, LazyItemAnimationSpecs specs = {});

    // The scroll position and programmatic scrolling shared by LazyListState and LazyGridState.
    class LazyStateBase : public ScrollableState {
    public:
        explicit LazyStateBase(int firstIndex, int firstOffset) : m_Index(firstIndex), m_Offset(firstOffset) {}
        int FirstVisibleItemIndex() const { return m_Index.Get(); }
        int FirstVisibleItemScrollOffset() const { return m_Offset.Get(); }
        float ConsumeScrollDelta(float delta) override;
        bool CanScrollForward() const override { return m_CanForward; }
        bool CanScrollBackward() const override { return m_Index.Peek() > 0 || m_Offset.Peek() > 0; }
        bool LaysOutOnScroll() const override { return true; }
        void ScrollToItem(int index, int offset = 0);
        // requestScrollToItem: the next measure starts at index/offset (by index, not key) without
        // stopping a scroll in progress.
        void RequestScrollToItem(int index, int offset = 0);
        // Scrolls in animated steps of up to 2500 dp (jumping to 100 items before the target when
        // further), then seeks the target once it is composed (LazyLayoutScrollScope).
        rt::Task<> AnimateScrollToItem(int index, int scrollOffset = 0);
        // The viewport along the scroll axis minus content padding (last measure).
        float ViewportLength() const { return m_ViewportLength; }

    protected:
        friend struct LazyCore;
        // After each measure (the pager derives its current page here).
        virtual void OnMeasured() {}
        struct Line { int first, last, offset, size; };   // visible lines, items first..last
        int LastVisibleIndex() const { return m_Lines.empty() ? 0 : m_Lines.back().last; }
        int DistanceTo(int index, int targetOffset = 0) const;
        bool Visible(int index) const { return index >= m_Index.Peek() && index <= LastVisibleIndex(); }
        void SnapToItem(int index, int offset);

        rt::State<int> m_Index, m_Offset;
        float m_PendingScroll = 0;
        float m_LastConsumed = 0;
        bool m_CanForward = true;
        rt::Key m_FirstKey = rt::Key::Positional();
        bool m_HasFirstKey = false;
        LayoutNode* m_Node = nullptr;   // the layout, for forced remeasure
        std::vector<Line> m_Lines;
        int m_SlotsPerLine = 1, m_Spacing = 0, m_TotalItems = 0;
        float m_Density = 1;
        // Pager: the fixed page size, and a scroll position given as a fraction of a page that
        // the next measure turns into pixels.
        int m_PageSize = 0;
        std::optional<float> m_PendingFraction;
        float m_ViewportLength = 0;
    };

    class LazyListState final : public LazyStateBase {
    public:
        explicit LazyListState(int firstIndex = 0, int firstOffset = 0) : LazyStateBase(firstIndex, firstOffset) {}
        // Reading subscribes; updated by every measure.
        const LazyListLayoutInfo& LayoutInfo() const { return m_Info.Get(); }
    private:
        friend struct LazyCore;
        rt::State<LazyListLayoutInfo> m_Info{ LazyListLayoutInfo{} };
    };

    class LazyGridState final : public LazyStateBase {
    public:
        explicit LazyGridState(int firstIndex = 0, int firstOffset = 0) : LazyStateBase(firstIndex, firstOffset) {}
        const LazyGridLayoutInfo& LayoutInfo() const { return m_Info.Get(); }
    private:
        friend struct LazyCore;
        rt::State<LazyGridLayoutInfo> m_Info{ LazyGridLayoutInfo{} };
    };

    struct LazyInterval {
        int count;
        std::function<void(int)> content;
        std::function<rt::Key(int)> key;
        bool sticky = false;
        // Grid only: the span for local index i given (maxCurrentLineSpan, maxLineSpan).
        std::function<int(int i, int maxCurrentLineSpan, int maxLineSpan)> span;
    };

    class LazyListScope {
    public:
        void Item(std::function<void()> content, rt::Key key = rt::Key::Positional());
        void Items(int count, std::function<void(int)> content, std::function<rt::Key(int)> key = {});
        // stickyHeader: stays pinned at the start while the items after it scroll, until the
        // next header pushes it out. `content` gets the header's index in the list.
        void StickyHeader(std::function<void(int index)> content, rt::Key key = rt::Key::Positional());

        using Interval = LazyInterval;
        std::vector<Interval> intervals;
    };

    // GridItemSpanScope: maxCurrentLineSpan is what is left on the current line (a larger span
    // moves the item to the next line), maxLineSpan the slots per line.
    struct GridItemSpanScope { int maxCurrentLineSpan, maxLineSpan; };
    using GridSpan = std::function<int(const GridItemSpanScope&, int index)>;

    class LazyGridScope {
    public:
        void Item(std::function<void()> content, rt::Key key = rt::Key::Positional(),
                  std::function<int(const GridItemSpanScope&)> span = {});
        void Items(int count, std::function<void(int)> content, std::function<rt::Key(int)> key = {}, GridSpan span = {});
        // A full-line header that sticks like LazyColumn's.
        void StickyHeader(std::function<void(int index)> content, rt::Key key = rt::Key::Positional());

        std::vector<LazyInterval> intervals;
    };

    // GridCells: how the cross axis is split into slots.
    struct GridCells {
        enum class Kind : std::uint8_t { Fixed, Adaptive, FixedSize } kind = Kind::Fixed;
        int count = 1;
        Dp size{ 0 };
        static GridCells Fixed(int count) { return { Kind::Fixed, std::max(1, count), {} }; }
        // As many slots as fit with at least minSize each.
        static GridCells Adaptive(Dp minSize) { return { Kind::Adaptive, 1, minSize }; }
        // Slots of exactly `size` (one slot of the full size if it does not fit).
        static GridCells FixedSize(Dp size) { return { Kind::FixedSize, 1, size }; }
        std::vector<int> CellSizes(const Density& d, int available, int spacing) const;
        bool operator==(const GridCells&) const = default;
    };

    struct LazyListOptions {
        PaddingValues contentPadding;
        // verticalArrangement (LazyColumn) / horizontalArrangement (LazyRow): its spacing goes
        // between items; the whole arrangement applies when the items do not fill the viewport.
        // Compose's default for reverseLayout is Bottom (End).
        Arrangement arrangement = Arrangement::Top();
        bool reverseLayout = false;   // item 0 at the bottom (end); scrolling reversed
        AlignmentH horizontalAlignment = AlignmentH::Start();   // LazyColumn
        AlignmentV verticalAlignment = AlignmentV::Top();       // LazyRow
        bool userScrollEnabled = true;
    };

    struct LazyGridOptions {
        PaddingValues contentPadding;
        // Between lines (its spacing) and across them (spacing and placement of the slots).
        Arrangement mainAxisArrangement = Arrangement::Top();
        Arrangement crossAxisArrangement = Arrangement::Start();
        bool reverseLayout = false;
        bool userScrollEnabled = true;
    };

    // Internal: the pager's use of the lazy core (Pager.cpp).
    struct LazyPagerConfig {
        PaddingValues contentPadding;
        Dp pageSpacing{ 0 };
        std::function<int(const Density&, int available, int spacing)> pageSize;   // PageSize.calculateMainAxisPageSize
        AlignmentH horizontalAlignment = AlignmentH::CenterHorizontally();
        AlignmentV verticalAlignment = AlignmentV::CenterVertically();
        bool userScrollEnabled = true;
        FlingBehavior flingBehavior;
        std::function<Modifier(Modifier)> extraModifier;   // gesture observers
    };
    void LazyPagerLayout(bool vertical, const Modifier& modifier, LazyStateBase& state, int pageCount,
                         std::function<void(int page)> content, std::function<rt::Key(int)> key, LazyPagerConfig config, rt::Key compositionKey);

    void LazyColumn(const Modifier& modifier, LazyListState& state, std::function<void(LazyListScope&)> content,
                    LazyListOptions options = {}, rt::Key key = {});
    void LazyRow(const Modifier& modifier, LazyListState& state, std::function<void(LazyListScope&)> content,
                 LazyListOptions options = {}, rt::Key key = {});
    void LazyVerticalGrid(const Modifier& modifier, GridCells columns, LazyGridState& state,
                          std::function<void(LazyGridScope&)> content, LazyGridOptions options = {}, rt::Key key = {});
    void LazyHorizontalGrid(const Modifier& modifier, GridCells rows, LazyGridState& state,
                            std::function<void(LazyGridScope&)> content, LazyGridOptions options = {}, rt::Key key = {});

}
