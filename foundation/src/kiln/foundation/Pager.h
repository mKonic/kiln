#pragma once

// HorizontalPager / VerticalPager (pager/*.kt): a lazy row (column) of pages of a fixed size
// that settles on a page at its snap position. A release flings to the next page past
// 400 dp/s, otherwise it returns to the page it started on unless dragged more than half a page
// (PagerDefaults.flingBehavior: PagerSnapDistance.atMost(1), snap spring StiffnessMediumLow).

#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Snapping.h"

namespace kiln::foundation {

    class PagerState final : public LazyStateBase {
    public:
        explicit PagerState(int currentPage = 0, float currentPageOffsetFraction = 0, std::function<int()> pageCount = {});

        int PageCount() const { return m_PageCountFn ? m_PageCountFn() : m_PageCount; }
        // The page closest to the snap position, and how far (in pages, -0.5..0.5) it is from it.
        int CurrentPage() const { return m_Current.Get(); }
        float CurrentPageOffsetFraction() const { return m_Fraction.Get(); }
        // The page the pager last rested on; changes when a scroll ends.
        int SettledPage() const { return IsScrollInProgress() ? m_Settled.Get() : m_Current.Get(); }
        // Where a scroll in progress is heading (currentPage when idle).
        int TargetPage() const;
        int PageSize() const { return m_PageSize; }
        int PageSpacing() const { return m_Spacing; }

        void ScrollToPage(int page, float pageOffsetFraction = 0);
        // requestScrollToPage: the next measure starts at the page without stopping a scroll in
        // progress.
        void RequestScrollToPage(int page, float pageOffsetFraction = 0);
        rt::Task<> AnimateScrollToPage(int page, float pageOffsetFraction = 0, anim::SpecPtr spec = anim::Spring());

        float ConsumeScrollDelta(float delta) override;

        // Internal: the snap target of a release (SnapLayoutInfoProvider.calculateSnapOffset
        // with calculateFinalSnappingBound), the gesture's finger displacement along the axis
        // (upDownDifference) and whether a finger is down on the pager (isGestureInProgress).
        float SnapOffset(float velocity, float positionalThreshold = 0.5f) const;
        void SetPageCount(int n) { m_PageCount = n; }
        void SetPageCountProvider(std::function<int()> fn) { m_PageCountFn = std::move(fn); }
        void SetGestureDelta(float d) { m_UpDown = d; }
        void SetGestureInProgress(bool b) { m_GestureInProgress = b; }
        bool GestureInProgress() const { return m_GestureInProgress; }
        int FirstVisiblePage() const { return m_Index.Peek(); }
        int FirstVisiblePageOffset() const { return m_Offset.Peek(); }
        bool LastScrolledForward() const { return m_LastForward; }
        const ScrollIndicatorState* ScrollIndicator() const override { return &m_Indicator; }

    private:
        struct Indicator final : ScrollIndicatorState {
            const PagerState* s;
            explicit Indicator(const PagerState* state) : s(state) {}
            int ScrollOffset() const override;
            int ContentSize() const override;
            int ViewportSize() const override;
        };
        Indicator m_Indicator{ this };
        void OnMeasured() override;
        bool ScrollingForward(float velocity) const;

        std::function<int()> m_PageCountFn;
        int m_PageCount = 0;
        rt::State<int> m_Current, m_Settled;
        rt::State<float> m_Fraction{ 0 };
        rt::State<int> m_ProgrammaticTarget{ -1 };
        float m_UpDown = 0;
        bool m_LastForward = false;
        bool m_GestureInProgress = false;
    };

    // PagerSnapDistance: where a fling may settle given the page it started from and the page a
    // natural decay would reach. AtMost(n): at most n pages from the start page.
    struct PagerSnapDistance {
        std::function<int(int startPage, int suggestedTargetPage, float velocity, int pageSize, int pageSpacing)> calculateTargetPage;
        static PagerSnapDistance AtMost(int pages);
    };

    // PagerDefaults.flingBehavior: decay towards the page the snap distance allows, then a spring
    // (StiffnessMediumLow) onto it. snapPositionalThreshold: the fraction of a page a slow release
    // must pass to settle on the next one.
    FlingBehavior PagerFlingBehavior(PagerState& state, PagerSnapDistance snapDistance = PagerSnapDistance::AtMost(1),
                                     float snapPositionalThreshold = 0.5f);

    // PagerDefaults.pageNestedScrollConnection: a drag in a page's scrollable first settles the
    // pager onto a page; leftovers after the gesture and children's flings never move the pager.
    std::shared_ptr<NestedScrollConnection> PageNestedScrollConnection(PagerState& state, Orientation orientation);

    // PageSize: Fill takes the viewport (minus content padding), Fixed a given size, Custom
    // computes it from the available space and page spacing in pixels
    // (calculateMainAxisPageSize, called on each measure).
    struct PageSize {
        bool fill = true;
        Dp size{ 0 };
        std::function<int(const Density&, int availableSpace, int pageSpacing)> calculate;
        static PageSize Fill() { return {}; }
        static PageSize Fixed(Dp d) { return { false, d, {} }; }
        static PageSize Custom(std::function<int(const Density&, int, int)> f) { return { false, Dp{ 0 }, std::move(f) }; }
    };

    struct PagerOptions {
        PaddingValues contentPadding;
        PageSize pageSize = PageSize::Fill();
        Dp pageSpacing{ 0 };
        AlignmentV verticalAlignment = AlignmentV::CenterVertically();       // HorizontalPager
        AlignmentH horizontalAlignment = AlignmentH::CenterHorizontally();   // VerticalPager
        bool userScrollEnabled = true;
        // Page 0 at the end (bottom, or the end edge); scrolling and paging go the other way.
        bool reverseLayout = false;
        std::function<rt::Key(int page)> key;
        OverscrollParam overscrollEffect;   // unset: rememberOverscrollEffect(); null: none
        // Pages composed and laid out on each side beyond the visible ones (ready before they
        // scroll in, and keeping their state).
        int beyondViewportPageCount = 0;
        SnapPosition snapPosition = SnapPosition::Start();   // where the current page rests
        FlingBehavior flingBehavior;                          // null: PagerFlingBehavior(state)
        std::shared_ptr<NestedScrollConnection> pageNestedScrollConnection;   // null: PageNestedScrollConnection
    };

    void HorizontalPager(const Modifier& modifier, PagerState& state, std::function<void(int page)> content,
                         PagerOptions options = {}, rt::Key key = {});
    void VerticalPager(const Modifier& modifier, PagerState& state, std::function<void(int page)> content,
                       PagerOptions options = {}, rt::Key key = {});

    // rememberPagerState: the current page and its offset fraction survive re-creation; the page
    // count is read from `pageCount` (refreshed every composition).
    PagerState& RememberPagerState(int initialPage, float initialPageOffsetFraction, std::function<int()> pageCount);
    inline PagerState& RememberPagerState(std::function<int()> pageCount) { return RememberPagerState(0, 0, std::move(pageCount)); }

}
