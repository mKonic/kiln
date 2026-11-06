#pragma once

// HorizontalPager / VerticalPager (pager/*.kt): a lazy row (column) of pages of a fixed size
// that settles on a page. A release flings to the next page past 400 dp/s, otherwise it returns
// to the page it started on unless dragged more than half a page (PagerDefaults.flingBehavior
// with PagerSnapDistance.atMost(1), snap spring StiffnessMediumLow).
// Not yet: pageNestedScrollConnection, custom snap positions and snap distances (PLAN M5).

#include "kiln/foundation/Lazy.h"

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
        rt::Task<> AnimateScrollToPage(int page, float pageOffsetFraction = 0, anim::SpecPtr spec = anim::Spring());

        float ConsumeScrollDelta(float delta) override;

        // Internal: the snap target of a release (SnapLayoutInfoProvider.calculateSnapOffset)
        // and the gesture's finger displacement along the axis (upDownDifference).
        float SnapOffset(float velocity) const;
        void SetPageCount(int n) { m_PageCount = n; }
        void SetGestureDelta(float d) { m_UpDown = d; }

    private:
        void OnMeasured() override;
        bool ScrollingForward(float velocity) const;

        std::function<int()> m_PageCountFn;
        int m_PageCount = 0;
        rt::State<int> m_Current, m_Settled;
        rt::State<float> m_Fraction{ 0 };
        rt::State<int> m_ProgrammaticTarget{ -1 };
        float m_UpDown = 0;
        bool m_LastForward = false;
    };

    // PageSize: Fill takes the viewport (minus content padding), Fixed a given size.
    struct PageSize {
        bool fill = true;
        Dp size{ 0 };
        static PageSize Fill() { return {}; }
        static PageSize Fixed(Dp d) { return { false, d }; }
    };

    struct PagerOptions {
        PaddingValues contentPadding;
        PageSize pageSize = PageSize::Fill();
        Dp pageSpacing{ 0 };
        AlignmentV verticalAlignment = AlignmentV::CenterVertically();       // HorizontalPager
        AlignmentH horizontalAlignment = AlignmentH::CenterHorizontally();   // VerticalPager
        bool userScrollEnabled = true;
        std::function<rt::Key(int page)> key;
        OverscrollParam overscrollEffect;   // unset: rememberOverscrollEffect(); null: none
        // Pages composed and laid out on each side beyond the visible ones (ready before they
        // scroll in, and keeping their state).
        int beyondViewportPageCount = 0;
    };

    void HorizontalPager(const Modifier& modifier, PagerState& state, std::function<void(int page)> content,
                         PagerOptions options = {}, rt::Key key = {});
    void VerticalPager(const Modifier& modifier, PagerState& state, std::function<void(int page)> content,
                       PagerOptions options = {}, rt::Key key = {});

}
