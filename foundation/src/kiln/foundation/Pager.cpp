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
        // ScrollPosition holds the fraction from the start: it reads back before the first layout.
        if (fraction != 0) {
            m_PendingFraction = std::clamp(fraction, -0.5f, 0.5f);
            m_Fraction.Set(*m_PendingFraction);
        }
    }

    float PagerState::ConsumeScrollDelta(float delta) {
        if (delta != 0) m_LastForward = delta > 0;
        return LazyStateBase::ConsumeScrollDelta(delta);
    }

    // calculateNewCurrentPage / currentPageOffsetFraction (PagerMeasure.kt): the page closest to
    // the snap position, and how far it is from it.
    void PagerState::OnMeasured() {
        int current = m_Current.Peek();
        float fraction = 0;
        if (!m_Lines.empty()) {
            const Line* best = &m_Lines.front();
            for (const auto& l : m_Lines) if (std::abs(l.offset - m_PageSnap) < std::abs(best->offset - m_PageSnap)) best = &l;
            current = best->first;
            const int withSpacing = m_PageSize + m_Spacing;
            fraction = withSpacing == 0 ? 0.0f : std::clamp(float(m_PageSnap - best->offset) / float(withSpacing), -0.5f, 0.5f);
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
        const float displacement = float(DistanceTo(target) - m_PageSnap) + fraction * float(m_PageSize + m_Spacing);
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
    float PagerState::SnapOffset(float velocity, float positionalThreshold) const {
        constexpr float inf = std::numeric_limits<float>::infinity();
        float lower = -inf, upper = inf;
        for (const auto& l : m_Lines) {
            const float d = float(l.offset - m_PageSnap);   // calculateDistanceToDesiredSnapPosition
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
            if (std::abs(overflow) > positionalThreshold) result = forward ? upper : lower;
            else if (std::abs(offsetFromSnapped) >= threshold) result = forward ? lower : upper;
            else result = std::abs(lower) < std::abs(upper) ? lower : upper;
        } else {
            result = velocity > 0 ? upper : lower;
        }
        return std::isfinite(result) ? result : 0.0f;
    }

    namespace {
        // Records the finger's displacement of each gesture before the scrollable sees the up.
        rt::Task<> PagerGestures(PointerInputScope& s, PagerState* st, bool vertical, bool rtl) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                if (e->changes.empty() || !e->changes[0].ChangedToDownIgnoreConsumed()) continue;
                const std::int64_t id = e->changes[0].id;
                const Offset down = e->changes[0].position;
                st->SetGestureDelta(0);
                st->SetGestureInProgress(true);
                for (;;) {
                    auto m = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                    const PointerInputChange* c = nullptr;
                    for (const auto& ch : m->changes) if (ch.id == id) c = &ch;
                    if (!c) { st->SetGestureInProgress(false); break; }
                    if (c->ChangedToUpIgnoreConsumed()) {
                        const Offset d = c->position - down;
                        // In scroll terms: RTL mirrors a horizontal pager's finger direction.
                        st->SetGestureDelta(vertical ? d.y : (rtl ? -d.x : d.x));
                        st->SetGestureInProgress(false);
                        break;
                    }
                }
            }
        }

        // SnapLayoutInfoProvider(pagerState, pagerSnapDistance) (PagerSnapLayoutInfoProvider.kt).
        class PagerSnapProvider final : public SnapLayoutInfoProvider {
        public:
            PagerSnapProvider(PagerState* s, PagerSnapDistance d, float threshold) : st(s), distance(std::move(d)), threshold(threshold) {}
            float CalculateApproachOffset(float velocity, float decayOffset) override {
                const int step = st->PageSize() + st->PageSpacing();
                if (step == 0) return 0;
                const int start = velocity < 0 ? st->FirstVisiblePage() + 1 : st->FirstVisiblePage();
                const int count = st->PageCount();
                const int target = std::clamp(start + int(decayOffset / float(step)), 0, count);
                int corrected = distance.calculateTargetPage ? distance.calculateTargetPage(start, target, velocity, st->PageSize(), st->PageSpacing()) : target;
                corrected = std::clamp(corrected, 0, count);
                const int proposed = (corrected - start) * step;
                const int approach = std::max(std::abs(proposed) - step, 0);
                return approach == 0 ? 0.0f : float(approach) * (velocity > 0 ? 1.0f : velocity < 0 ? -1.0f : 0.0f);
            }
            float CalculateSnapOffset(float velocity) override { return st->SnapOffset(velocity, threshold); }
        private:
            PagerState* st;
            PagerSnapDistance distance;
            float threshold;
        };

        // DefaultPagerNestedScrollConnection.
        class PageConnection final : public NestedScrollConnection {
        public:
            PageConnection(PagerState* s, Orientation o, bool rtl) : st(s), orientation(o), rtl(rtl) {}
            float Main(Offset o) const { return orientation == Orientation::Horizontal ? o.x : o.y; }
            Offset OnOrientation(Offset o) const { return orientation == Orientation::Vertical ? Offset{ 0, o.y } : Offset{ o.x, 0 }; }
            // A drag (not a fling) while between pages: the pager takes up to the next page boundary.
            Offset OnPreScroll(Offset available, NestedScrollSource source) override {
                const float fraction = st->CurrentPageOffsetFraction();
                if (source != NestedScrollSource::UserInput || std::abs(fraction) <= 1e-6f || Main(available) == 0) return {};
                const float current = fraction * float(st->PageSize());
                const float space = float(st->PageSize() + st->PageSpacing());
                const float next = current + space * (fraction > 0 ? -1.0f : 1.0f);
                const float lo = fraction > 0 ? next : current, hi = fraction > 0 ? current : next;
                const float coerced = std::clamp(Main(available), lo, hi);
                const float consumed = orientation == Orientation::Horizontal && rtl ? st->ConsumeScrollDelta(coerced)
                                                                                     : -st->ConsumeScrollDelta(-coerced);
                return orientation == Orientation::Horizontal ? Offset{ consumed, available.y } : Offset{ available.x, consumed };
            }
            // A child's fling leftover: Compose cancels the child's fling; here it is swallowed so
            // the pager does not move (the child runs out at its edge).
            Offset OnPostScroll(Offset, Offset available, NestedScrollSource source) override {
                if (Main(available) == 0) return {};
                if (source == NestedScrollSource::SideEffect || !st->GestureInProgress()) return OnOrientation(available);
                return {};
            }
            Offset OnPostFling(Offset, Offset available) override { return OnOrientation(available); }
        private:
            PagerState* st;
            Orientation orientation;
            bool rtl;
        };

        // DefaultPagerBringIntoViewSpec: a request lands on whole pages, and a satisfied one still
        // settles the pager onto a page.
        class PagerBringIntoView final : public BringIntoViewSpec {
        public:
            PagerBringIntoView(PagerState* s, bool flip, std::shared_ptr<const BringIntoViewSpec> d) : st(s), flip(flip), inner(std::move(d)) {}
            float CalculateScrollDistance(float offset, float size, float container) const override {
                const float proposed = inner->CalculateScrollDistance(offset, size, container);
                const bool out = offset > 0 ? offset + size > container : offset + size <= 1.0f;   // Int.VisibilityThreshold
                if (proposed != 0 && out) return Override(proposed);
                if (std::abs(float(st->FirstVisiblePageOffset())) < 1e-6f) return 0;
                return Settling(container);
            }
        private:
            int Step() const { return st->PageSize() + st->PageSpacing(); }
            // layoutAwareFirstOffset
            int First() const { return flip ? -st->FirstVisiblePageOffset() + Step() : st->FirstVisiblePageOffset(); }
            float Settling(float container) const {
                const float reversed = -float(First());
                const bool forward = st->LastScrolledForward();
                const float d = flip ? (forward ? reversed : reversed + float(Step())) : (forward ? reversed + float(Step()) : reversed);
                return std::clamp(d, -container, container);
            }
            float Override(float proposed) const {
                float corrected = -float(First());
                while (proposed > 0 && corrected < proposed) corrected += float(Step());
                while (proposed < 0 && corrected > proposed) corrected -= float(Step());
                return corrected;
            }
            PagerState* st;
            bool flip;   // RTL horizontal (shouldChangeScrollDirection)
            std::shared_ptr<const BringIntoViewSpec> inner;
        };

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
            const SnapPosition snap = options.snapPosition;
            config.snapPosition = [snap](int layout, int page, int before, int after) { return snap.Position(layout, page, before, after); };
            config.flingBehavior = options.flingBehavior ? options.flingBehavior : PagerFlingBehavior(state);
            const bool rtl = rt::Composition::Current() && rt::Composition::Current()->Read(ui::LocalLayoutDirection) == LayoutDirection::Rtl;
            config.bringIntoViewSpec = std::make_shared<PagerBringIntoView>(st, rtl && !vertical, CurrentBringIntoViewSpec());
            std::shared_ptr<NestedScrollConnection> connection = options.pageNestedScrollConnection;
            if (!connection) connection = PageNestedScrollConnection(state, vertical ? Orientation::Vertical : Orientation::Horizontal);
            config.extraModifier = [st, vertical, rtl, connection](Modifier m) {
                m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(st), vertical, rtl, 7),
                                 [st, vertical, rtl](PointerInputScope& s) { return PagerGestures(s, st, vertical, rtl); });
                return NestedScroll(std::move(m), connection);
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

    PagerSnapDistance PagerSnapDistance::AtMost(int pages) {
        const int limit = std::max(0, pages);
        return { [limit](int start, int suggested, float, int, int) {
            const long long lo = std::max(0LL, (long long)start - limit);
            const long long hi = std::min((long long)std::numeric_limits<int>::max(), (long long)start + limit);
            return int(std::clamp((long long)suggested, lo, hi));
        } };
    }

    FlingBehavior PagerFlingBehavior(PagerState& state, PagerSnapDistance snapDistance, float snapPositionalThreshold) {
        PagerState* st = &state;
        auto provider = std::make_shared<PagerSnapProvider>(st, std::move(snapDistance), std::clamp(snapPositionalThreshold, 0.0f, 1.0f));
        return SnapFlingBehavior(provider, [st] { return std::make_shared<anim::SplineDecay>(st->Density()); },
                                 anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 1.0f));
    }

    std::shared_ptr<NestedScrollConnection> PageNestedScrollConnection(PagerState& state, Orientation orientation) {
        auto* c = rt::Composition::Current();
        const bool rtl = c && c->Read(ui::LocalLayoutDirection) == LayoutDirection::Rtl;
        auto make = [&] { return std::shared_ptr<NestedScrollConnection>(std::make_shared<PageConnection>(&state, orientation, rtl)); };
        if (!c) return make();
        return c->Remember<std::shared_ptr<NestedScrollConnection>>(make, rt::Keys(reinterpret_cast<std::uintptr_t>(&state), int(orientation), rtl));
    }

    void HorizontalPager(const Modifier& m, PagerState& state, std::function<void(int)> content, PagerOptions o, rt::Key key) {
        Pager(false, m, state, std::move(content), std::move(o), key);
    }
    void VerticalPager(const Modifier& m, PagerState& state, std::function<void(int)> content, PagerOptions o, rt::Key key) {
        Pager(true, m, state, std::move(content), std::move(o), key);
    }

}
