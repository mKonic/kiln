#include "kiln/foundation/Pager.h"

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/ui/Locals.h"

#include <cmath>
#include <limits>

namespace kiln::foundation {

    namespace {
        constexpr float kMinFlingVelocityDp = 400;      // MinFlingVelocityDp
        constexpr float kPositionThresholdDp = 56;      // DefaultPositionThreshold
        constexpr int kMaxPagesForAnimateScroll = 3;
    }

    PagerState::PagerState(int currentPage, float fraction, std::function<int()> pageCount)
        : LazyStateBase(std::max(0, currentPage), 0), m_PageCountFn(std::move(pageCount)),
          m_Current(std::max(0, currentPage)), m_Settled(std::max(0, currentPage)) {
        if (fraction != 0) m_PendingFraction = std::clamp(fraction, -0.5f, 0.5f);
    }

    float PagerState::ConsumeScrollDelta(float delta) {
        if (delta != 0) m_LastForward = delta > 0;
        return LazyStateBase::ConsumeScrollDelta(delta);
    }

    // calculateNewCurrentPage / currentPageOffsetFraction (PagerMeasure.kt), snap position Start.
    void PagerState::OnMeasured() {
        int current = m_Current.Peek();
        float fraction = 0;
        if (!m_Lines.empty()) {
            const Line* best = &m_Lines.front();
            for (const auto& l : m_Lines) if (std::abs(l.offset) < std::abs(best->offset)) best = &l;
            current = best->first;
            const int withSpacing = m_PageSize + m_Spacing;
            fraction = withSpacing == 0 ? 0.0f : std::clamp(float(-best->offset) / float(withSpacing), -0.5f, 0.5f);
        }
        if (m_Current.Peek() != current) m_Current.Set(current);
        if (m_Fraction.Peek() != fraction) m_Fraction.Set(fraction);
        if (!IsScrollInProgress() && m_Settled.Peek() != current) m_Settled.Set(current);
    }

    int PagerState::TargetPage() const {
        int page;
        const int current = m_Current.Get();
        if (!IsScrollInProgress()) page = current;
        else if (m_ProgrammaticTarget.Get() != -1) page = m_ProgrammaticTarget.Get();
        else {
            const float threshold = m_PageSize == 0 ? 0.5f : std::min(kPositionThresholdDp * m_Density, m_PageSize / 2.0f) / float(m_PageSize);
            if (std::abs(m_Fraction.Get()) >= threshold) page = m_LastForward ? m_Index.Get() + 1 : m_Index.Get();
            else page = current;
        }
        return std::clamp(page, 0, std::max(0, PageCount() - 1));
    }

    void PagerState::ScrollToPage(int page, float fraction) {
        StopScroll();
        m_Index.Set(std::clamp(page, 0, std::max(0, PageCount() - 1)));
        m_Offset.Set(0);
        m_PendingFraction = std::clamp(fraction, -0.5f, 0.5f);
        m_HasFirstKey = false;
        m_PendingScroll = 0;
        if (m_Node) m_Node->RequestRemeasure();
    }

    rt::Task<> PagerState::AnimateScrollToPage(int page, float fraction, anim::SpecPtr spec) {
        if ((page == m_Current.Peek() && fraction == m_Fraction.Peek()) || PageCount() == 0) co_return;
        const int target = std::clamp(page, 0, PageCount() - 1);
        anim::Animatable a(0.0f);
        bool stopped = false;
        ScrollSession session(*this, [&] { stopped = true; a.Stop(); });
        m_ProgrammaticTarget.Set(target);
        // Pre-jump to a viewport away from far targets.
        const int first = m_Index.Peek(), last = LastVisibleIndex();
        const bool forward = target > first;
        const int visible = last - first + 1;
        if (((forward && target > last) || (!forward && target < first)) && std::abs(target - first) >= kMaxPagesForAnimateScroll)
            SnapToItem(forward ? std::max(target - visible, first) : std::min(target + visible, first), 0);
        const float displacement = float(DistanceTo(target)) + fraction * float(m_PageSize + m_Spacing);
        float previous = 0;
        co_await a.AnimateTo(anim::Vec{ displacement, 0, 0, 0 }, std::move(spec), {}, [&](const anim::Vec& v) {
            if (stopped) return;
            previous += ConsumeScrollDelta(v[0] - previous);
        });
        if (session.Current()) m_ProgrammaticTarget.Set(-1);
    }

    // isScrollingForward: the finger's displacement decides; without a gesture, the velocity.
    bool PagerState::ScrollingForward(float velocity) const {
        const bool gesture = int(m_UpDown) != 0;
        const bool positive = (gesture ? m_UpDown : -velocity) > 0;
        return !positive;
    }

    // SnapLayoutInfoProvider.calculateSnapOffset + calculateFinalSnappingBound (Pager.kt).
    float PagerState::SnapOffset(float velocity) const {
        constexpr float inf = std::numeric_limits<float>::infinity();
        float lower = -inf, upper = inf;
        for (const auto& l : m_Lines) {
            const float d = float(l.offset);
            if (d <= 0 && d > lower) lower = d;
            if (d >= 0 && d < upper) upper = d;
        }
        if (lower == -inf) lower = upper;
        if (upper == inf) upper = lower;
        const bool forward = ScrollingForward(velocity);
        if (!CanScrollForward()) { upper = 0; if (forward) lower = 0; }
        if (!CanScrollBackward()) { lower = 0; if (!forward) upper = 0; }

        const float offsetFromSnapped = m_PageSize == 0 ? 0.0f : m_UpDown / float(m_PageSize);
        const float overflow = offsetFromSnapped - float(int(offsetFromSnapped));
        float result;
        if (std::abs(velocity) < kMinFlingVelocityDp * m_Density) {
            const float threshold = m_PageSize == 0 ? 0.5f : std::min(kPositionThresholdDp * m_Density, m_PageSize / 2.0f) / float(m_PageSize);
            if (std::abs(overflow) > 0.5f) result = forward ? upper : lower;
            else if (std::abs(offsetFromSnapped) >= threshold) result = forward ? lower : upper;
            else result = std::abs(lower) < std::abs(upper) ? lower : upper;
        } else {
            result = velocity > 0 ? upper : lower;
        }
        return std::isfinite(result) ? result : 0.0f;
    }

    namespace {
        // snapFlingBehavior with no approach (atMost(1) never travels more than a page): a spring
        // to the snap offset carrying the fling velocity, stopping at the target.
        rt::Task<float> PagerFling(PagerState* st, std::function<float(float)> scrollBy, float velocity) {
            const float target = st->SnapOffset(velocity);
            if (target == 0) co_return 0.0f;
            anim::Animatable a(0.0f);
            float previous = 0;
            bool edge = false;
            co_await a.AnimateTo(anim::Vec{ target, 0, 0, 0 }, anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 1.0f),
                                 anim::Vec{ velocity, 0, 0, 0 }, [&](const anim::Vec& v) {
                float value = v[0];
                const bool reached = std::abs(value) >= std::abs(target);
                if (reached) value = target;
                const float delta = value - previous;
                const float consumed = scrollBy(delta);
                previous += delta;
                if (std::abs(delta - consumed) > 0.5f) { edge = true; a.Stop(); }
                else if (reached) a.Stop();
            });
            co_return edge ? a.Velocity()[0] : 0.0f;
        }

        // Records the finger's displacement of each gesture before the scrollable sees the up.
        rt::Task<> PagerGestures(PointerInputScope& s, PagerState* st, bool vertical, bool rtl) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                if (e->changes.empty() || !e->changes[0].ChangedToDownIgnoreConsumed()) continue;
                const std::int64_t id = e->changes[0].id;
                const Offset down = e->changes[0].position;
                st->SetGestureDelta(0);
                for (;;) {
                    auto m = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                    const PointerInputChange* c = nullptr;
                    for (const auto& ch : m->changes) if (ch.id == id) c = &ch;
                    if (!c) break;
                    if (c->ChangedToUpIgnoreConsumed()) {
                        const Offset d = c->position - down;
                        // In scroll terms: RTL mirrors a horizontal pager's finger direction.
                        st->SetGestureDelta(vertical ? d.y : (rtl ? -d.x : d.x));
                        break;
                    }
                }
            }
        }

        rt::Task<> PageTo(PagerState* st, int page) { co_await st->AnimateScrollToPage(page); }

        void Pager(bool vertical, const Modifier& modifier, PagerState& state, std::function<void(int)> content,
                   PagerOptions options, rt::Key key) {
            LazyPagerConfig config;
            config.contentPadding = options.contentPadding;
            config.pageSpacing = options.pageSpacing;
            const PageSize ps = options.pageSize;
            config.pageSize = [ps](const Density& d, int available, int) { return ps.fill ? available : d.RoundToPx(ps.size); };
            config.horizontalAlignment = options.horizontalAlignment;
            config.verticalAlignment = options.verticalAlignment;
            config.userScrollEnabled = options.userScrollEnabled;
            config.overscrollEffect = options.overscrollEffect;
            config.beyondViewportPageCount = options.beyondViewportPageCount;
            PagerState* st = &state;
            config.flingBehavior = [st](std::function<float(float)> scrollBy, float velocity) {
                return PagerFling(st, std::move(scrollBy), velocity);
            };
            const bool rtl = rt::Composition::Current() && rt::Composition::Current()->Read(ui::LocalLayoutDirection) == LayoutDirection::Rtl;
            config.extraModifier = [st, vertical, rtl](Modifier m) {
                return PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(st), vertical, rtl, 7),
                                    [st, vertical, rtl](PointerInputScope& s) { return PagerGestures(s, st, vertical, rtl); });
            };
            const int count = state.PageCount();
            // pagerSemantics: page up/down (vertical) or left/right to the previous/next page.
            Modifier m = modifier;
            if (options.userScrollEnabled) {
                auto& comp = *rt::Composition::Current();
                auto job = comp.Remember<std::shared_ptr<rt::Job>>([] { return std::make_shared<rt::Job>(); });
                m = Semantics(std::move(m), [st, vertical, job](SemanticsConfiguration& c) {
                    auto back = [st, job] {
                        if (!st->CanScrollBackward()) return false;
                        *job = rt::Launch(rt::Dispatcher::Main(), PageTo(st, std::max(0, st->CurrentPage() - 1)));
                        return true;
                    };
                    auto forward = [st, job] {
                        if (!st->CanScrollForward()) return false;
                        *job = rt::Launch(rt::Dispatcher::Main(), PageTo(st, std::min(st->PageCount() - 1, st->CurrentPage() + 1)));
                        return true;
                    };
                    if (vertical) { c.pageUp = back; c.pageDown = forward; }
                    else { c.pageLeft = back; c.pageRight = forward; }
                });
            }
            LazyPagerLayout(vertical, m, state, count, std::move(content), options.key, std::move(config), key);
        }
    }

    void HorizontalPager(const Modifier& m, PagerState& state, std::function<void(int)> content, PagerOptions o, rt::Key key) {
        Pager(false, m, state, std::move(content), std::move(o), key);
    }
    void VerticalPager(const Modifier& m, PagerState& state, std::function<void(int)> content, PagerOptions o, rt::Key key) {
        Pager(true, m, state, std::move(content), std::move(o), key);
    }

}
