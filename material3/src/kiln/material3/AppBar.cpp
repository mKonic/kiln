#include "kiln/material3/AppBar.h"

#include "kiln/animation/Easing.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draggable.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Scaffold.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/tokens/AppBarLargeFlexibleTokens.h"
#include "kiln/material3/tokens/AppBarLargeTokens.h"
#include "kiln/material3/tokens/AppBarMediumFlexibleTokens.h"
#include "kiln/material3/tokens/AppBarMediumTokens.h"
#include "kiln/material3/tokens/AppBarSmallTokens.h"
#include "kiln/material3/tokens/AppBarTokens.h"
#include "kiln/material3/tokens/BottomAppBarTokens.h"
#include "kiln/material3/tokens/FabSecondaryContainerTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/ColorSpace.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        Color Or(Color c, Color fallback) { return c.Specified() ? c : fallback; }

        constexpr Dp MediumTitleBottomPadding{ 24 }, LargeTitleBottomPadding{ 28 };
        constexpr Dp TopAppBarHorizontalPadding{ 4 };
        // The title's inset on medium and large bars, and its start space without a navigation icon.
        constexpr Dp TopAppBarTitleInset{ 16 - 4 };
        // Padding minus the content padding (BottomAppBarHorizontalPadding / VerticalPadding).
        constexpr Dp FABHorizontalPadding{ 16 - 4 }, FABVerticalPadding{ 12 - 4 };

        const anim::Easing& TopTitleAlphaEasing() {
            static const anim::Easing easing = anim::Easing::CubicBezier(0.8f, 0, 0.8f, 0.15f);
            return easing;
        }

        bool Usable(Dp d) { return d.Specified() && std::isfinite(d.value); }

        // --- settling (settleAppBar / settleAppBarBottom) -----------------------------------------

        // The fling left from the content first carries on with the decay; then the bar snaps to
        // expanded or collapsed, whichever is nearer. Returns the velocity left.
        template<typename S>
        rt::Task<float> Settle(std::shared_ptr<S> state, float velocity, std::shared_ptr<const anim::DecaySpec> fling, anim::SpecPtr snap) {
            // Fully expanded or collapsed (not 0: the fraction's float precision).
            if (state->CollapsedFraction() < 0.01f || state->CollapsedFraction() == 1.0f) co_return 0.0f;
            float remaining = velocity;
            if (fling && std::abs(velocity) > 1) {
                anim::Animatable a(0.0f);
                float last = 0;
                auto r = co_await a.AnimateDecay(velocity, fling, [&](float value) {
                    const float delta = value - last;
                    const float initial = state->HeightOffset();
                    state->SetHeightOffset(initial + delta);
                    const float consumed = std::abs(initial - state->HeightOffset());
                    last = value;
                    // Stop once anything goes unconsumed (and against rounding).
                    if (std::abs(delta - consumed) > 0.5f) a.Stop();
                });
                remaining = r.reason == anim::EndReason::Finished ? 0.0f : r.endVelocity[0];
            }
            if (snap && state->HeightOffset() < 0 && state->HeightOffset() > state->HeightOffsetLimit()) {
                anim::Animatable a(state->HeightOffset());
                const float target = state->CollapsedFraction() < 0.5f ? 0.0f : state->HeightOffsetLimit();
                co_await a.AnimateTo(anim::Vec{ target, 0, 0, 0 }, snap, std::nullopt, [&](const anim::Vec& v) { state->SetHeightOffset(v[0]); });
            }
            co_return remaining;
        }

        // --- top app bar scroll behaviours ------------------------------------------------------

        struct TopBehaviorBase : TopAppBarScrollBehavior, ui::NestedScrollConnection, std::enable_shared_from_this<TopBehaviorBase> {
            std::shared_ptr<TopAppBarState> state;
            std::function<bool()> canScroll;
            anim::SpecPtr snap;
            std::shared_ptr<const anim::DecaySpec> fling;
            bool pinned = false;
            TopAppBarState& State() const override { return *state; }
            bool IsPinned() const override { return pinned; }
            anim::SpecPtr SnapAnimationSpec() const override { return snap; }
            std::shared_ptr<const anim::DecaySpec> FlingAnimationSpec() const override { return fling; }
            std::shared_ptr<ui::NestedScrollConnection> NestedScrollConnection() const override {
                // The behaviour is its own connection, kept alive by whoever holds it.
                return std::shared_ptr<ui::NestedScrollConnection>(std::const_pointer_cast<TopBehaviorBase>(shared_from_this()),
                                                                   const_cast<TopBehaviorBase*>(this));
            }
            bool CanScroll() const { return !canScroll || canScroll(); }
        };

        // PinnedScrollBehavior: only counts the content's scroll (for the colour).
        struct Pinned final : TopBehaviorBase {
            ui::Offset OnPostScroll(ui::Offset consumed, ui::Offset, ui::NestedScrollSource) override {
                if (!CanScroll()) return {};
                state->SetContentOffset(state->ContentOffset() + consumed.y);
                return {};
            }
            rt::Task<ui::Offset> OnPostFling(ui::Offset, ui::Offset available) override {
                // Scrolled all the way down: zero the total, against float drift.
                if (available.y > 0) state->SetContentOffset(0);
                co_return ui::Offset{};
            }
        };

        // EnterAlwaysScrollBehavior: the bar takes the scroll first, both ways.
        struct EnterAlways final : TopBehaviorBase {
            ui::Offset OnPreScroll(ui::Offset available, ui::NestedScrollSource) override {
                if (!CanScroll()) return {};
                const float prev = state->HeightOffset();
                state->SetHeightOffset(prev + available.y);
                // It moved: it is collapsing or expanding, and takes the vertical part.
                return prev != state->HeightOffset() ? ui::Offset{ 0, available.y } : ui::Offset{};
            }
            ui::Offset OnPostScroll(ui::Offset consumed, ui::Offset, ui::NestedScrollSource) override {
                if (!CanScroll()) return {};
                state->SetContentOffset(state->ContentOffset() + consumed.y);
                return {};
            }
            rt::Task<ui::Offset> OnPostFling(ui::Offset, ui::Offset available) override {
                if (available.y > 0) state->SetContentOffset(0);
                const float left = co_await Settle(state, available.y, fling, snap);
                co_return ui::Offset{ 0, left };
            }
        };

        // ExitUntilCollapsedScrollBehavior: collapses with the content's upward scroll, expands
        // only once the content has none left above.
        struct ExitUntilCollapsed final : TopBehaviorBase {
            ui::Offset OnPreScroll(ui::Offset available, ui::NestedScrollSource) override {
                // Not when scrolling down.
                if (!CanScroll() || available.y > 0) return {};
                const float prev = state->HeightOffset();
                state->SetHeightOffset(prev + available.y);
                return prev != state->HeightOffset() ? ui::Offset{ 0, available.y } : ui::Offset{};
            }
            ui::Offset OnPostScroll(ui::Offset consumed, ui::Offset available, ui::NestedScrollSource) override {
                if (!CanScroll()) return {};
                state->SetContentOffset(state->ContentOffset() + consumed.y);
                if (available.y < 0 || consumed.y < 0) {
                    // Scrolling up: follow what the content consumed.
                    const float old = state->HeightOffset();
                    state->SetHeightOffset(old + consumed.y);
                    return { 0, state->HeightOffset() - old };
                }
                if (available.y > 0) {
                    // What the content could not use expands the bar.
                    const float old = state->HeightOffset();
                    state->SetHeightOffset(old + available.y);
                    return { 0, state->HeightOffset() - old };
                }
                return {};
            }
            rt::Task<ui::Offset> OnPostFling(ui::Offset, ui::Offset available) override {
                if (available.y > 0) state->SetContentOffset(0);
                const float left = co_await Settle(state, available.y, fling, snap);
                co_return ui::Offset{ 0, left };
            }
        };

        template<typename B>
        std::shared_ptr<TopAppBarScrollBehavior> RememberTopBehavior(std::shared_ptr<TopAppBarState> state, std::function<bool()> canScroll,
                                                                     anim::SpecPtr snap, std::shared_ptr<const anim::DecaySpec> fling,
                                                                     std::function<bool()> isScrollingContentAtStart, bool pinned) {
            auto& c = *rt::Composition::Current();
            if (!state) state = RememberTopAppBarState();
            // remember(state, canScroll, snap, fling): the lambdas compare by presence.
            auto& b = c.Remember<std::shared_ptr<B>>([] { return std::make_shared<B>(); },
                                                     rt::Keys(state.get(), snap.get(), fling.get(), bool(canScroll)));
            b->state = state;
            b->canScroll = std::move(canScroll);
            b->snap = std::move(snap);
            b->fling = std::move(fling);
            b->pinned = pinned;
            if (isScrollingContentAtStart) state->isScrollingContentAtStart = std::move(isScrollingContentAtStart);
            return b;
        }

        rt::Task<> SettleTop(std::shared_ptr<TopAppBarState> state, float velocity, std::shared_ptr<const anim::DecaySpec> fling, anim::SpecPtr snap) {
            co_await Settle(state, velocity, std::move(fling), std::move(snap));
        }
        rt::Task<> SettleBottom(std::shared_ptr<BottomAppBarState> state, float velocity, std::shared_ptr<const anim::DecaySpec> fling, anim::SpecPtr snap) {
            co_await Settle(state, velocity, std::move(fling), std::move(snap));
        }

        // Resizing the bar by dragging it.
        ui::Modifier AppBarDrag(ui::Modifier m, const std::shared_ptr<TopAppBarScrollBehavior>& behavior) {
            if (!behavior || behavior->IsPinned()) return m;
            auto& state = behavior->State();
            auto scope = internal::RememberCoroutineScope(rt::Key(7001));
            DraggableState& drag = RememberDraggableState([&state](float delta) { state.SetHeightOffset(state.HeightOffset() + delta); });
            DraggableOptions o;
            o.onDragStopped = [behavior, scope](float velocity) {
                auto& s = behavior->State();
                // The state outlives the drag: the behaviour holds it.
                std::shared_ptr<TopAppBarState> keep(std::shared_ptr<TopAppBarScrollBehavior>(behavior), &s);
                scope->Launch(SettleTop(keep, velocity, behavior->FlingAnimationSpec(), behavior->SnapAnimationSpec()));
            };
            return Draggable(std::move(m), drag, Orientation::Vertical, o);
        }

        // adjustHeightOffsetLimit: the bar can collapse by its own height.
        ui::Modifier AdjustHeightOffsetLimit(ui::Modifier m, const std::shared_ptr<TopAppBarScrollBehavior>& behavior) {
            if (!behavior) return m;
            TopAppBarState* state = &behavior->State();
            return OnSizeChanged(std::move(m), [state, keep = behavior](ui::IntSize size) {
                state->SetHeightOffsetLimit(-(float(size.height) - state->HeightOffset()));
            });
        }

        // TopAppBarMeasurePolicy: the navigation icon at the start, the actions at the end, the
        // title between them (aligned on the whole bar), its height less the scroll offset.
        class TopAppBarPolicy final : public ui::MeasurePolicy {
        public:
            enum class TitleArrangement : std::uint8_t { Center, Bottom };
            TopAppBarPolicy(std::function<float()> scrolledOffset, TitleArrangement arrangement, AlignmentH alignment, int titleBottomPadding, Dp height,
                            PaddingValues contentPadding)
                : m_ScrolledOffset(std::move(scrolledOffset)), m_Arrangement(arrangement), m_Alignment(alignment), m_TitleBottomPadding(titleBottomPadding),
                  m_Height(height), m_ContentPadding(contentPadding) {}

            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                const ui::Constraints loose{ 0, c.maxWidth, c.minHeight, c.maxHeight };
                ui::Placeable& nav = children[0]->Measure(loose);
                ui::Placeable& actions = children[2]->Measure(loose);
                const ui::LayoutDirection dir = s.Direction();
                const int startPadding = s.RoundToPx(Start(dir)), endPadding = s.RoundToPx(End(dir));
                const int titleStartSpace = std::max(s.RoundToPx(TopAppBarTitleInset), nav.Width());
                const int maxTitleWidth = c.maxWidth == ui::Constraints::Infinity
                                              ? c.maxWidth
                                              : std::max(c.maxWidth - titleStartSpace - actions.Width() - startPadding - endPadding, 0);
                ui::Placeable& title = children[1]->Measure(ui::Constraints{ 0, maxTitleWidth, c.minHeight, c.maxHeight });
                // The title's baseline (0 without one).
                const int lastBaseline = title.Get(ui::LastBaseline);
                const int titleBaseline = lastBaseline == ui::AlignmentLine::Unspecified ? 0 : lastBaseline;
                // The scrolled offset is at most zero; it takes from the height.
                const float scrolled = m_ScrolledOffset ? m_ScrolledOffset() : 0.0f;
                const int heightOffset = std::isnan(scrolled) ? 0 : ui::RoundJava(scrolled);
                const int topPadding = s.RoundToPx(m_ContentPadding.top), bottomPadding = s.RoundToPx(m_ContentPadding.bottom);
                const int maxLayoutHeight = std::max(s.RoundToPx(m_Height), title.Height()) + topPadding + bottomPadding;
                const int layoutHeight = c.maxHeight == ui::Constraints::Infinity ? maxLayoutHeight : std::max(maxLayoutHeight + heightOffset, 0);
                return Place(s, c, layoutHeight, maxLayoutHeight, nav, title, actions, titleBaseline);
            }
            int MinIntrinsicWidth(ui::MeasureScope&, std::span<ui::Measurable* const> children, int height) override {
                int w = 0;
                for (auto* m : children) w += m->MinIntrinsicWidth(height);
                return w;
            }
            int MaxIntrinsicWidth(ui::MeasureScope&, std::span<ui::Measurable* const> children, int height) override {
                int w = 0;
                for (auto* m : children) w += m->MaxIntrinsicWidth(height);
                return w;
            }
            int MinIntrinsicHeight(ui::MeasureScope& s, std::span<ui::Measurable* const> children, int width) override {
                int h = 0;
                for (auto* m : children) h = std::max(h, m->MinIntrinsicHeight(width));
                return std::max(s.RoundToPx(m_Height), h);
            }
            int MaxIntrinsicHeight(ui::MeasureScope& s, std::span<ui::Measurable* const> children, int width) override {
                int h = 0;
                for (auto* m : children) h = std::max(h, m->MaxIntrinsicHeight(width));
                return std::max(s.RoundToPx(m_Height), h);
            }

        private:
            Dp Start(ui::LayoutDirection d) const { return m_ContentPadding.rtlAware || d == ui::LayoutDirection::Ltr ? m_ContentPadding.start : m_ContentPadding.end; }
            Dp End(ui::LayoutDirection d) const { return m_ContentPadding.rtlAware || d == ui::LayoutDirection::Ltr ? m_ContentPadding.end : m_ContentPadding.start; }

            ui::MeasureResult Place(ui::MeasureScope& s, const ui::Constraints& c, int layoutHeight, int maxLayoutHeight, ui::Placeable& nav,
                                    ui::Placeable& title, ui::Placeable& actions, int titleBaseline) const {
                const ui::LayoutDirection dir = s.Direction();
                const int topPadding = s.RoundToPx(m_ContentPadding.top), bottomPadding = s.RoundToPx(m_ContentPadding.bottom);
                const int startPadding = s.RoundToPx(Start(dir)), endPadding = s.RoundToPx(End(dir));
                const int contentHeight = layoutHeight + topPadding - bottomPadding;
                const int width = c.maxWidth;
                const int start = std::max(s.RoundToPx(TopAppBarTitleInset), nav.Width()), end = actions.Width();
                // Aligned on the whole width (LTR: placed relative below), then moved clear of the
                // navigation icon and the actions: a centred title stays centred on the bar.
                int titleX = m_Alignment.Align(title.Width(), width, ui::LayoutDirection::Ltr);
                if (titleX < start) titleX += startPadding + (start - titleX);
                else if (titleX + title.Width() > width - end) titleX += startPadding + ((width - end) - (titleX + title.Width()));
                int titleY;
                if (m_Arrangement == TitleArrangement::Center) {
                    titleY = (contentHeight - title.Height()) / 2;
                } else if (m_TitleBottomPadding == 0) {
                    titleY = contentHeight - title.Height();
                } else {
                    // The padding is from the baseline; smaller when the title does not fit.
                    const int paddingFromBottom = m_TitleBottomPadding - (title.Height() - titleBaseline);
                    const int heightWithPadding = paddingFromBottom + title.Height();
                    const int adjusted = heightWithPadding > maxLayoutHeight ? paddingFromBottom - (heightWithPadding - maxLayoutHeight) : paddingFromBottom;
                    titleY = contentHeight - title.Height() - std::max(0, adjusted);
                }
                return s.Layout(width, layoutHeight, [&nav, &title, &actions, startPadding, endPadding, contentHeight, titleX, titleY, width](ui::PlacementScope& p) {
                    p.PlaceRelative(nav, startPadding, (contentHeight - nav.Height()) / 2);
                    p.PlaceRelative(title, titleX, titleY);
                    p.PlaceRelative(actions, width - actions.Width() - endPadding, (contentHeight - actions.Height()) / 2);
                });
            }

            std::function<float()> m_ScrolledOffset;
            TitleArrangement m_Arrangement;
            AlignmentH m_Alignment;
            int m_TitleBottomPadding;
            Dp m_Height;
            PaddingValues m_ContentPadding;
        };

        struct LayoutParams {
            std::function<float()> scrolledOffset;
            Color navigationIconContentColor, titleContentColor, subtitleContentColor, actionIconContentColor;
            std::function<void()> title;
            text::TextStyle titleTextStyle;
            std::function<void()> subtitle;
            text::TextStyle subtitleTextStyle;
            std::function<float()> titleAlpha;
            TopAppBarPolicy::TitleArrangement titleArrangement = TopAppBarPolicy::TitleArrangement::Center;
            AlignmentH titleHorizontalAlignment = AlignmentH::Start();
            int titleBottomPadding = 0;
            bool hideTitleSemantics = false;
            std::function<void()> navigationIcon, actions;
            Dp height;
            PaddingValues contentPadding;
        };

        void TopAppBarLayout(const ui::Modifier& modifier, const LayoutParams& p) {
            auto policy = std::make_shared<TopAppBarPolicy>(p.scrolledOffset, p.titleArrangement, p.titleHorizontalAlignment, p.titleBottomPadding, p.height,
                                                            p.contentPadding);
            foundation::Layout(modifier, policy, [p] {
                Box(Padding({}, PaddingValues{ TopAppBarHorizontalPadding, Dp(0), Dp(0), Dp(0) }), Alignment::TopStart(), false, [&p] {
                    rt::Composition::Current()->Provide(LocalContentColor, p.navigationIconContentColor, p.navigationIcon ? p.navigationIcon : [] {});
                });
                ui::Modifier titleMod = Padding({}, TopAppBarHorizontalPadding, Dp(0));
                if (p.hideTitleSemantics) titleMod = ClearAndSetSemantics(std::move(titleMod), [](ui::SemanticsConfiguration&) {});
                else if (p.subtitle) titleMod = Semantics(std::move(titleMod), [](ui::SemanticsConfiguration& s) { s.isTraversalGroup = true; });
                const auto alpha = p.titleAlpha;
                titleMod = GraphicsLayer(std::move(titleMod), [alpha](GraphicsLayerParams& l) { l.alpha = alpha ? alpha() : 1.0f; });
                if (p.subtitle) {
                    Column(titleMod, Arrangement::Top(), p.titleHorizontalAlignment, [&p] {
                        internal::ProvideContentColorTextStyle(p.titleContentColor, p.titleTextStyle, p.title);
                        internal::ProvideContentColorTextStyle(p.subtitleContentColor, p.subtitleTextStyle, p.subtitle);
                    });
                } else {
                    Box(titleMod, Alignment::TopStart(), false, [&p] { internal::ProvideContentColorTextStyle(p.titleContentColor, p.titleTextStyle, p.title); });
                }
                Box(Padding({}, PaddingValues{ Dp(0), Dp(0), TopAppBarHorizontalPadding, Dp(0) }), Alignment::TopStart(), false, [&p] {
                    rt::Composition::Current()->Provide(LocalContentColor, p.actionIconContentColor, p.actions ? p.actions : [] {});
                });
            });
        }

        // The actions wrapped in a row at the end.
        std::function<void()> ActionsRow(std::function<void()> actions) {
            return [actions] { Row({}, Arrangement::End(), AlignmentV::CenterVertically(), actions ? actions : [] {}); };
        }

        rt::Task<> Nothing(PointerInputScope&) { co_return; }

        struct SingleRowParams {
            std::function<void()> title;
            text::TextStyle titleTextStyle;
            std::function<void()> subtitle;
            text::TextStyle subtitleTextStyle;
            AlignmentH titleHorizontalAlignment;
            std::function<void()> navigationIcon, actions;
            Dp expandedHeight;
            PaddingValues contentPadding;
            WindowInsets windowInsets;
            TopAppBarColors colors;
            std::shared_ptr<TopAppBarScrollBehavior> scrollBehavior;
        };

        void SingleRowTopAppBar(const ui::Modifier& modifier, const SingleRowParams& p) {
            auto& c = *rt::Composition::Current();
            const auto behavior = p.scrollBehavior;
            const TopAppBarColors colors = p.colors;
            // The container colour flips once content scrolls under the bar (derivedStateOf: the
            // bar recomposes then, not every scroll frame), and animates there.
            auto& target = c.RememberNew<rt::Derived<Color>>(rt::Keys(behavior.get(), colors.containerColor.argb, colors.scrolledContainerColor.argb), {},
                                                             [behavior, colors] {
                                                                 const float overlapping = behavior ? behavior->State().OverlappedFraction() : 0.0f;
                                                                 return colors.ContainerColor(overlapping > 0.01f ? 1.0f : 0.0f);
                                                             });
            const Color containerColor = AnimateColorAsState(target.Get(), Value(MotionSchemeKeyTokens::DefaultEffects));
            ui::Modifier m = AppBarDrag(modifier, behavior);
            m = DrawBehind(std::move(m), [containerColor](ui::DrawScope& d) { if (containerColor.Specified()) d.DrawRect(containerColor, {}, d.GetSize()); });
            m = Semantics(std::move(m), [](ui::SemanticsConfiguration& s) { s.isTraversalGroup = true; });
            m = PointerInput(std::move(m), rt::Keys(), [](PointerInputScope& s) { return Nothing(s); });
            LayoutParams lp;
            lp.scrolledOffset = [behavior] { return behavior ? behavior->State().HeightOffset() : 0.0f; };
            lp.navigationIconContentColor = colors.navigationIconContentColor;
            lp.titleContentColor = colors.titleContentColor;
            lp.actionIconContentColor = colors.actionIconContentColor;
            lp.subtitleContentColor = colors.subtitleContentColor;
            lp.title = p.title;
            lp.titleTextStyle = p.titleTextStyle;
            lp.subtitle = p.subtitle;
            lp.subtitleTextStyle = p.subtitleTextStyle;
            lp.titleAlpha = [] { return 1.0f; };
            lp.titleArrangement = TopAppBarPolicy::TitleArrangement::Center;
            lp.titleHorizontalAlignment = p.titleHorizontalAlignment;
            lp.navigationIcon = p.navigationIcon;
            lp.actions = ActionsRow(p.actions);
            lp.height = p.expandedHeight;
            lp.contentPadding = p.contentPadding;
            const WindowInsets insets = p.windowInsets;
            Box(m, Alignment::TopStart(), false, [lp, insets, behavior] {
                // Clipped after the padding, so the title does not show over the inset.
                TopAppBarLayout(AdjustHeightOffsetLimit(ClipToBounds(WindowInsetsPadding({}, insets)), behavior), lp);
            });
        }

        struct TwoRowsParams {
            std::function<void()> title;
            text::TextStyle titleTextStyle;
            Dp titleBottomPadding;
            std::function<void()> smallTitle;
            text::TextStyle smallTitleTextStyle;
            std::function<void()> subtitle;
            text::TextStyle subtitleTextStyle;
            std::function<void()> smallSubtitle;
            text::TextStyle smallSubtitleTextStyle;
            AlignmentH titleHorizontalAlignment;
            std::function<void()> navigationIcon, actions;
            Dp collapsedHeight, expandedHeight;
            WindowInsets windowInsets;
            TopAppBarColors colors;
            std::shared_ptr<TopAppBarScrollBehavior> scrollBehavior;
        };

        void TwoRowsTopAppBarImpl(const ui::Modifier& modifier, const TwoRowsParams& p) {
            auto& c = *rt::Composition::Current();
            const auto behavior = p.scrollBehavior;
            const TopAppBarColors colors = p.colors;
            const int titleBottomPaddingPx = c.Read(ui::LocalDensity).RoundToPx(p.titleBottomPadding);
            // The colour follows the collapse, read where it draws.
            const auto fraction = [behavior] { return behavior ? behavior->State().CollapsedFraction() : 0.0f; };
            // The top row's title shows from halfway; the bottom row's semantics hide then.
            auto& hideTopRow = c.RememberNew<rt::Derived<bool>>(rt::Keys(behavior.get()), {}, [fraction] { return fraction() < 0.5f; });
            const bool hideTopRowSemantics = hideTopRow.Get();
            ui::Modifier m = AppBarDrag(modifier, behavior);
            m = DrawBehind(std::move(m), [fraction, colors](ui::DrawScope& d) { d.DrawRect(colors.ContainerColor(fraction()), {}, d.GetSize()); });
            m = Semantics(std::move(m), [](ui::SemanticsConfiguration& s) { s.isTraversalGroup = true; });
            m = PointerInput(std::move(m), rt::Keys(), [](PointerInputScope& s) { return Nothing(s); });

            LayoutParams top;
            top.scrolledOffset = [] { return 0.0f; };
            top.navigationIconContentColor = colors.navigationIconContentColor;
            top.titleContentColor = colors.titleContentColor;
            top.actionIconContentColor = colors.actionIconContentColor;
            top.subtitleContentColor = colors.subtitleContentColor;
            top.title = p.smallTitle;
            top.titleTextStyle = p.smallTitleTextStyle;
            top.subtitle = p.smallSubtitle;
            top.subtitleTextStyle = p.smallSubtitleTextStyle;
            top.titleAlpha = [fraction] { return TopTitleAlphaEasing().Transform(fraction()); };
            top.titleArrangement = TopAppBarPolicy::TitleArrangement::Center;
            top.titleHorizontalAlignment = p.titleHorizontalAlignment;
            top.hideTitleSemantics = hideTopRowSemantics;
            top.navigationIcon = p.navigationIcon;
            top.actions = ActionsRow(p.actions);
            top.height = p.collapsedHeight;
            top.contentPadding = TopAppBarDefaults::ContentPadding;

            LayoutParams bottom = top;
            bottom.scrolledOffset = [behavior] { return behavior ? behavior->State().HeightOffset() : 0.0f; };
            bottom.title = p.title;
            bottom.titleTextStyle = p.titleTextStyle;
            bottom.subtitle = p.subtitle;
            bottom.subtitleTextStyle = p.subtitleTextStyle;
            bottom.titleAlpha = [fraction] { return 1 - fraction(); };
            bottom.titleArrangement = TopAppBarPolicy::TitleArrangement::Bottom;
            bottom.titleBottomPadding = titleBottomPaddingPx;
            bottom.hideTitleSemantics = !hideTopRowSemantics;
            bottom.navigationIcon = [] {};
            bottom.actions = [] {};
            bottom.height = Dp(p.expandedHeight.value - p.collapsedHeight.value);

            const WindowInsets insets = p.windowInsets;
            Box(m, Alignment::TopStart(), false, [top, bottom, insets, behavior] {
                Column({}, [&] {
                    TopAppBarLayout(ClipToBounds(WindowInsetsPadding({}, insets)), top);
                    // Only the sides of the insets: the row above takes the top.
                    TopAppBarLayout(AdjustHeightOffsetLimit(ClipToBounds(WindowInsetsPadding({}, insets.Only(WindowInsetsSides::Horizontal()))), behavior),
                                    bottom);
                });
            });
        }

        Dp Or(Dp d, Dp fallback) { return Usable(d) ? d : fallback; }

        void TwoRows(std::function<void()> title, const ui::Modifier& modifier, const TwoRowsTopAppBarOptions& o, TypographyToken titleFont,
                     Dp titleBottomPadding, std::function<void()> subtitle, std::optional<TypographyToken> subtitleFont, Dp collapsedDefault,
                     Dp expandedDefault, AlignmentH alignment, std::function<void()> smallTitle = {}, std::function<void()> smallSubtitle = {}) {
            TwoRowsParams p;
            p.title = title;
            p.titleTextStyle = Value(titleFont);
            p.titleBottomPadding = titleBottomPadding;
            p.smallTitle = smallTitle ? smallTitle : title;
            p.smallTitleTextStyle = Value(AppBarSmallTokens::TitleFont);
            p.subtitle = subtitle;
            p.subtitleTextStyle = subtitleFont ? Value(*subtitleFont) : text::TextStyle{};
            p.smallSubtitle = smallSubtitle ? smallSubtitle : subtitle;
            p.smallSubtitleTextStyle = subtitleFont ? Value(AppBarSmallTokens::SubtitleFont) : text::TextStyle{};
            p.titleHorizontalAlignment = alignment;
            p.navigationIcon = o.navigationIcon;
            p.actions = o.actions;
            p.collapsedHeight = Or(o.collapsedHeight, collapsedDefault);
            p.expandedHeight = Or(o.expandedHeight, expandedDefault);
            // The expanded height is at least the collapsed one (require in Compose).
            p.expandedHeight = Dp(std::max(p.expandedHeight.value, p.collapsedHeight.value));
            p.windowInsets = o.windowInsets.value_or(TopAppBarDefaults::windowInsets());
            p.colors = o.colors.value_or(TopAppBarDefaults::topAppBarColors());
            p.scrollBehavior = o.scrollBehavior;
            TwoRowsTopAppBarImpl(modifier, p);
        }

        // --- bottom app bar ---------------------------------------------------------------------

        // ExitAlwaysScrollBehavior: the bar hides with the content's scroll and comes back.
        struct ExitAlways final : BottomAppBarScrollBehavior, ui::NestedScrollConnection, std::enable_shared_from_this<ExitAlways> {
            std::shared_ptr<BottomAppBarState> state;
            std::function<bool()> canScroll;
            anim::SpecPtr snap;
            std::shared_ptr<const anim::DecaySpec> fling;
            BottomAppBarState& State() const override { return *state; }
            bool IsPinned() const override { return false; }
            anim::SpecPtr SnapAnimationSpec() const override { return snap; }
            std::shared_ptr<const anim::DecaySpec> FlingAnimationSpec() const override { return fling; }
            std::shared_ptr<ui::NestedScrollConnection> NestedScrollConnection() const override {
                return std::shared_ptr<ui::NestedScrollConnection>(std::const_pointer_cast<ExitAlways>(shared_from_this()), const_cast<ExitAlways*>(this));
            }
            ui::Offset OnPostScroll(ui::Offset consumed, ui::Offset, ui::NestedScrollSource) override {
                if (canScroll && !canScroll()) return {};
                state->SetContentOffset(state->ContentOffset() + consumed.y);
                state->SetHeightOffset(state->HeightOffset() + consumed.y);
                return {};
            }
            rt::Task<ui::Offset> OnPostFling(ui::Offset, ui::Offset available) override {
                if (available.y > 0 && (state->HeightOffset() == 0 || state->HeightOffset() == state->HeightOffsetLimit())) state->SetContentOffset(0);
                const float left = co_await Settle(state, available.y, fling, snap);
                co_return ui::Offset{ 0, left };
            }
        };

        struct BottomLayoutParams {
            Dp containerHeight;
            Arrangement horizontalArrangement;
            Color containerColor, contentColor;
            Dp tonalElevation;
            PaddingValues contentPadding;
            WindowInsets windowInsets;
            std::shared_ptr<BottomAppBarScrollBehavior> scrollBehavior;
        };

        void BottomAppBarLayout(const ui::Modifier& modifier, const BottomLayoutParams& p, std::function<void()> content) {
            auto& c = *rt::Composition::Current();
            // Off for touch exploration: the bar stays where it can be found.
            const auto manager = c.Read(ui::LocalAccessibilityManager);
            const bool touchExploration = manager && ui::AccessibilityServiceOn(manager->ServiceState(), true, false, false);
            const auto behavior = touchExploration ? nullptr : p.scrollBehavior;
            ui::Modifier m = modifier;
            // The bar's height less its offset, the offset limited to its height.
            m = LayoutMod(std::move(m), [behavior](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
                ui::Placeable& placeable = measurable.Measure(c);
                if (behavior) behavior->State().SetHeightOffsetLimit(-float(placeable.Height()));
                const float height = std::max(0.0f, float(placeable.Height()) + (behavior ? behavior->State().HeightOffset() : 0.0f));
                return s.Layout(placeable.Width(), ui::RoundJava(height), [&placeable](ui::PlacementScope& place) { place.Place(placeable, 0, 0); });
            });
            if (behavior && !behavior->IsPinned()) {
                auto scope = internal::RememberCoroutineScope(rt::Key(7002));
                auto* state = &behavior->State();
                DraggableState& drag = RememberDraggableState([state](float delta) { state->SetHeightOffset(state->HeightOffset() - delta); });
                DraggableOptions o;
                o.onDragStopped = [behavior, scope](float velocity) {
                    std::shared_ptr<BottomAppBarState> keep(std::shared_ptr<BottomAppBarScrollBehavior>(behavior), &behavior->State());
                    scope->Launch(SettleBottom(keep, velocity, behavior->FlingAnimationSpec(), behavior->SnapAnimationSpec()));
                };
                m = Draggable(std::move(m), drag, Orientation::Vertical, o);
            }
            SurfaceOptions surface;
            surface.color = p.containerColor;
            surface.contentColor = p.contentColor;
            surface.tonalElevation = p.tonalElevation;
            surface.shape = Value(BottomAppBarTokens::ContainerShape);
            Surface(m, surface, [p, content = std::move(content)] {
                ui::Modifier row = WindowInsetsPadding(FillMax({}, Axis::Horizontal), p.windowInsets);
                row = Padding(Height(std::move(row), p.containerHeight), p.contentPadding);
                Row(row, p.horizontalArrangement, AlignmentV::CenterVertically(), content);
            });
        }
    }

    // --- states and colours ----------------------------------------------------------------------

    TopAppBarState::TopAppBarState(float limit, float offset, float content) : m_HeightOffsetLimit(limit), m_HeightOffset(offset), m_ContentOffset(content) {}
    void TopAppBarState::SetHeightOffset(float offset) { m_HeightOffset.Set(std::clamp(offset, std::min(m_HeightOffsetLimit, 0.0f), 0.0f)); }
    float TopAppBarState::CollapsedFraction() const { return m_HeightOffsetLimit != 0 ? HeightOffset() / m_HeightOffsetLimit : 0.0f; }
    float TopAppBarState::OverlappedFraction() const {
        if (!isScrollingContentAtStart() && ContentOffset() == 0) return 1;
        if (m_HeightOffsetLimit == 0) return 0;
        return 1 - std::clamp(m_HeightOffsetLimit + std::abs(ContentOffset()), m_HeightOffsetLimit, 0.0f) / m_HeightOffsetLimit;
    }

    std::shared_ptr<TopAppBarState> RememberTopAppBarState(float limit, float offset, float content, rt::Key key) {
        rt::Saver<std::shared_ptr<TopAppBarState>> saver{
            [](const std::shared_ptr<TopAppBarState>& s) {
                return rt::SavedValue(rt::SavedValue::List{ rt::SavedValue(double(s->HeightOffsetLimit())), rt::SavedValue(double(s->HeightOffset())),
                                                           rt::SavedValue(double(s->ContentOffset())) });
            },
            [](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<TopAppBarState>> {
                const auto* list = std::get_if<rt::SavedValue::List>(&saved.v);
                if (!list || list->size() != 3) return std::nullopt;
                const auto a = rt::SavedNumber((*list)[0]), b = rt::SavedNumber((*list)[1]), c = rt::SavedNumber((*list)[2]);
                if (!a || !b || !c) return std::nullopt;
                return std::make_shared<TopAppBarState>(float(*a), float(*b), float(*c));
            }
        };
        return rt::RememberSaveable<std::shared_ptr<TopAppBarState>>([=] { return std::make_shared<TopAppBarState>(limit, offset, content); }, saver, {}, key);
    }

    TopAppBarColors TopAppBarColors::Copy(Color a, Color b, Color c, Color d, Color e, Color f) const {
        return { Or(a, containerColor), Or(b, scrolledContainerColor), Or(c, navigationIconContentColor), Or(d, titleContentColor),
                 Or(e, actionIconContentColor), Or(f, subtitleContentColor) };
    }
    Color TopAppBarColors::ContainerColor(float fraction) const {
        return ui::Lerp(containerColor, scrolledContainerColor, anim::FastOutLinearIn().Transform(fraction));
    }

    BottomAppBarState::BottomAppBarState(float limit, float offset, float content) : m_HeightOffsetLimit(limit), m_HeightOffset(offset), m_ContentOffset(content) {}
    void BottomAppBarState::SetHeightOffset(float offset) {
        m_HeightOffset.Set(std::clamp(offset, std::min(m_HeightOffsetLimit.Peek(), 0.0f), 0.0f));
    }
    float BottomAppBarState::CollapsedFraction() const {
        const float limit = HeightOffsetLimit();
        return limit != 0 ? HeightOffset() / limit : 0.0f;
    }
    std::shared_ptr<BottomAppBarState> RememberBottomAppBarState(float limit, float offset, float content, rt::Key key) {
        rt::Saver<std::shared_ptr<BottomAppBarState>> saver{
            [](const std::shared_ptr<BottomAppBarState>& s) {
                return rt::SavedValue(rt::SavedValue::List{ rt::SavedValue(double(s->HeightOffsetLimit())), rt::SavedValue(double(s->HeightOffset())),
                                                           rt::SavedValue(double(s->ContentOffset())) });
            },
            [](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<BottomAppBarState>> {
                const auto* list = std::get_if<rt::SavedValue::List>(&saved.v);
                if (!list || list->size() != 3) return std::nullopt;
                const auto a = rt::SavedNumber((*list)[0]), b = rt::SavedNumber((*list)[1]), c = rt::SavedNumber((*list)[2]);
                if (!a || !b || !c) return std::nullopt;
                return std::make_shared<BottomAppBarState>(float(*a), float(*b), float(*c));
            }
        };
        return rt::RememberSaveable<std::shared_ptr<BottomAppBarState>>([=] { return std::make_shared<BottomAppBarState>(limit, offset, content); }, saver, {}, key);
    }

    // --- defaults ------------------------------------------------------------------------------

    namespace TopAppBarDefaults {
        TopAppBarColors topAppBarColors() {
            return { Value(AppBarTokens::ContainerColor), Value(AppBarTokens::OnScrollContainerColor), Value(AppBarTokens::LeadingIconColor),
                     Value(AppBarTokens::TitleColor),     Value(AppBarTokens::TrailingIconColor),      Value(AppBarTokens::SubtitleColor) };
        }
        TopAppBarColors topAppBarColors(Color a, Color b, Color c, Color d, Color e, Color f) { return topAppBarColors().Copy(a, b, c, d, e, f); }
        WindowInsets windowInsets() { return ScaffoldDefaults::contentWindowInsets().Only(WindowInsetsSides::Horizontal() + WindowInsetsSides::Top()); }
        anim::SpecPtr snapAnimationSpec() { return Value(MotionSchemeKeyTokens::DefaultEffects); }
        std::shared_ptr<const anim::DecaySpec> flingAnimationSpec() {
            // rememberSplineBasedDecay: the platform scroller's curve at this density.
            const float density = rt::Composition::Current()->Read(ui::LocalDensity).density;
            return rt::Composition::Current()->Remember<std::shared_ptr<const anim::DecaySpec>>(
                [density] { return std::make_shared<anim::SplineDecay>(density); }, rt::Keys(density));
        }

        std::shared_ptr<TopAppBarScrollBehavior> pinnedScrollBehavior(std::shared_ptr<TopAppBarState> state, std::function<bool()> canScroll,
                                                                      std::function<bool()> isScrollingContentAtStart) {
            return RememberTopBehavior<Pinned>(std::move(state), std::move(canScroll), nullptr, nullptr, std::move(isScrollingContentAtStart), true);
        }
        std::shared_ptr<TopAppBarScrollBehavior> enterAlwaysScrollBehavior(std::shared_ptr<TopAppBarState> state, std::function<bool()> canScroll,
                                                                           std::optional<anim::SpecPtr> snap,
                                                                           std::optional<std::shared_ptr<const anim::DecaySpec>> fling,
                                                                           std::function<bool()> isScrollingContentAtStart) {
            return RememberTopBehavior<EnterAlways>(std::move(state), std::move(canScroll), snap.value_or(snapAnimationSpec()),
                                                    fling.value_or(flingAnimationSpec()), std::move(isScrollingContentAtStart), false);
        }
        namespace {
            std::function<bool()> AtStart(foundation::ScrollableState& s) {
                return [&s] {
                    const foundation::ScrollIndicatorState* i = s.ScrollIndicator();
                    return (i ? i->ScrollOffset() : 0) == 0;
                };
            }
        }
        std::shared_ptr<TopAppBarScrollBehavior> pinnedScrollBehavior(foundation::ScrollableState& scrollableState, std::shared_ptr<TopAppBarState> state,
                                                                      std::function<bool()> canScroll) {
            return pinnedScrollBehavior(std::move(state), std::move(canScroll), AtStart(scrollableState));
        }
        std::shared_ptr<TopAppBarScrollBehavior> enterAlwaysScrollBehavior(foundation::ScrollableState& scrollableState,
                                                                           std::shared_ptr<TopAppBarState> state, std::function<bool()> canScroll,
                                                                           std::optional<anim::SpecPtr> snap,
                                                                           std::optional<std::shared_ptr<const anim::DecaySpec>> fling) {
            return enterAlwaysScrollBehavior(std::move(state), std::move(canScroll), std::move(snap), std::move(fling), AtStart(scrollableState));
        }
        std::shared_ptr<TopAppBarScrollBehavior> exitUntilCollapsedScrollBehavior(std::shared_ptr<TopAppBarState> state, std::function<bool()> canScroll,
                                                                                  std::optional<anim::SpecPtr> snap,
                                                                                  std::optional<std::shared_ptr<const anim::DecaySpec>> fling) {
            return RememberTopBehavior<ExitUntilCollapsed>(std::move(state), std::move(canScroll), snap.value_or(snapAnimationSpec()),
                                                           fling.value_or(flingAnimationSpec()), {}, false);
        }
    }

    namespace BottomAppBarDefaults {
        Color containerColor() { return Value(BottomAppBarTokens::ContainerColor); }
        WindowInsets windowInsets() { return ScaffoldDefaults::contentWindowInsets().Only(WindowInsetsSides::Horizontal() + WindowInsetsSides::Bottom()); }
        Color bottomAppBarFabColor() { return Value(FabSecondaryContainerTokens::ContainerColor); }
        anim::SpecPtr snapAnimationSpec() { return Value(MotionSchemeKeyTokens::FastSpatial); }
        std::shared_ptr<const anim::DecaySpec> flingAnimationSpec() { return TopAppBarDefaults::flingAnimationSpec(); }
        std::shared_ptr<BottomAppBarScrollBehavior> exitAlwaysScrollBehavior(std::shared_ptr<BottomAppBarState> state, std::function<bool()> canScroll,
                                                                             std::optional<anim::SpecPtr> snap,
                                                                             std::optional<std::shared_ptr<const anim::DecaySpec>> fling) {
            auto& c = *rt::Composition::Current();
            if (!state) state = RememberBottomAppBarState();
            const anim::SpecPtr s = snap.value_or(snapAnimationSpec());
            const auto f = fling.value_or(flingAnimationSpec());
            auto& b = c.Remember<std::shared_ptr<ExitAlways>>([] { return std::make_shared<ExitAlways>(); },
                                                              rt::Keys(state.get(), s.get(), f.get(), bool(canScroll)));
            b->state = state;
            b->canScroll = std::move(canScroll);
            b->snap = s;
            b->fling = f;
            return b;
        }
    }

    // --- top app bars ----------------------------------------------------------------------------

    namespace {
        void SingleRow(std::function<void()> title, const ui::Modifier& modifier, const TopAppBarOptions& o, AlignmentH alignment) {
            SingleRowParams p;
            p.title = std::move(title);
            p.titleTextStyle = Value(AppBarSmallTokens::TitleFont);
            p.subtitle = o.subtitle;
            p.subtitleTextStyle = o.subtitle ? Value(AppBarSmallTokens::SubtitleFont) : text::TextStyle{};
            p.titleHorizontalAlignment = alignment;
            p.navigationIcon = o.navigationIcon;
            p.actions = o.actions;
            p.expandedHeight = Or(o.expandedHeight, TopAppBarDefaults::TopAppBarExpandedHeight);
            p.contentPadding = o.contentPadding;
            p.windowInsets = o.windowInsets.value_or(TopAppBarDefaults::windowInsets());
            p.colors = o.colors.value_or(TopAppBarDefaults::topAppBarColors());
            p.scrollBehavior = o.scrollBehavior;
            SingleRowTopAppBar(modifier, p);
        }
    }

    void TopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TopAppBarOptions& o) {
        SingleRow(std::move(title), modifier, o, o.titleHorizontalAlignment);
    }
    void CenterAlignedTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TopAppBarOptions& o) {
        SingleRow(std::move(title), modifier, o, AlignmentH::CenterHorizontally());
    }

    void MediumTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TwoRowsTopAppBarOptions& o) {
        TwoRows(std::move(title), modifier, o, AppBarMediumTokens::TitleFont, MediumTitleBottomPadding, {}, std::nullopt,
                TopAppBarDefaults::MediumAppBarCollapsedHeight, TopAppBarDefaults::MediumAppBarExpandedHeight, AlignmentH::Start());
    }
    void MediumFlexibleTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TwoRowsTopAppBarOptions& o) {
        const Dp expanded = o.subtitle ? TopAppBarDefaults::MediumFlexibleAppBarWithSubtitleExpandedHeight
                                       : TopAppBarDefaults::MediumFlexibleAppBarWithoutSubtitleExpandedHeight;
        TwoRows(std::move(title), modifier, o, AppBarMediumFlexibleTokens::TitleFont, MediumTitleBottomPadding, o.subtitle ? o.subtitle : [] {},
                AppBarMediumFlexibleTokens::SubtitleFont, TopAppBarDefaults::MediumAppBarCollapsedHeight, expanded, o.titleHorizontalAlignment);
    }
    void LargeTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TwoRowsTopAppBarOptions& o) {
        TwoRows(std::move(title), modifier, o, AppBarLargeTokens::TitleFont, LargeTitleBottomPadding, {}, std::nullopt,
                TopAppBarDefaults::LargeAppBarCollapsedHeight, TopAppBarDefaults::LargeAppBarExpandedHeight, AlignmentH::Start());
    }
    void LargeFlexibleTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TwoRowsTopAppBarOptions& o) {
        const Dp expanded = o.subtitle ? TopAppBarDefaults::LargeFlexibleAppBarWithSubtitleExpandedHeight
                                       : TopAppBarDefaults::LargeFlexibleAppBarWithoutSubtitleExpandedHeight;
        TwoRows(std::move(title), modifier, o, AppBarLargeFlexibleTokens::TitleFont, LargeTitleBottomPadding, o.subtitle ? o.subtitle : [] {},
                AppBarLargeFlexibleTokens::SubtitleFont, TopAppBarDefaults::LargeAppBarCollapsedHeight, expanded, o.titleHorizontalAlignment);
    }
    void TwoRowsTopAppBar(std::function<void(bool)> title, const ui::Modifier& modifier, std::function<void(bool)> subtitle, const TwoRowsTopAppBarOptions& o) {
        const Dp expanded = subtitle ? TopAppBarDefaults::MediumFlexibleAppBarWithSubtitleExpandedHeight
                                     : TopAppBarDefaults::MediumFlexibleAppBarWithoutSubtitleExpandedHeight;
        TwoRows([title] { title(true); }, modifier, o, AppBarMediumFlexibleTokens::TitleFont, Dp(0), [subtitle] { if (subtitle) subtitle(true); },
                AppBarMediumFlexibleTokens::SubtitleFont, TopAppBarDefaults::MediumAppBarCollapsedHeight, expanded, o.titleHorizontalAlignment,
                [title] { title(false); }, [subtitle] { if (subtitle) subtitle(false); });
    }

    // --- bottom app bars -------------------------------------------------------------------------

    void BottomAppBar(const ui::Modifier& modifier, const BottomAppBarOptions& o, std::function<void()> content) {
        BottomLayoutParams p;
        p.containerHeight = BottomAppBarTokens::ContainerHeight;
        p.horizontalArrangement = Arrangement::Start();
        p.containerColor = o.containerColor.value_or(BottomAppBarDefaults::containerColor());
        p.contentColor = o.contentColor.value_or(ContentColorFor(p.containerColor));
        p.tonalElevation = o.tonalElevation;
        p.contentPadding = o.contentPadding;
        p.windowInsets = o.windowInsets.value_or(BottomAppBarDefaults::windowInsets());
        p.scrollBehavior = o.scrollBehavior;
        BottomAppBarLayout(modifier, p, std::move(content));
    }

    void BottomAppBar(std::function<void()> actions, const ui::Modifier& modifier, std::function<void()> floatingActionButton, const BottomAppBarOptions& o) {
        BottomAppBar(modifier, o, [actions, floatingActionButton] {
            Row(Weight({}, 1), Arrangement::Start(), AlignmentV::CenterVertically(), actions ? actions : [] {});
            if (floatingActionButton)
                Box(Padding(FillMax({}, Axis::Vertical), PaddingValues{ Dp(0), FABVerticalPadding, FABHorizontalPadding, Dp(0) }), Alignment::TopStart(), false,
                    floatingActionButton);
        });
    }

    void FlexibleBottomAppBar(const ui::Modifier& modifier, const FlexibleBottomAppBarOptions& o, std::function<void()> content) {
        BottomLayoutParams p;
        p.containerHeight = Usable(o.expandedHeight) && o.expandedHeight.value > 0 ? o.expandedHeight : BottomAppBarDefaults::FlexibleBottomAppBarHeight;
        p.horizontalArrangement = o.horizontalArrangement;
        p.containerColor = o.containerColor.value_or(BottomAppBarDefaults::containerColor());
        p.contentColor = o.contentColor.value_or(ContentColorFor(p.containerColor));
        p.tonalElevation = AppBarTokens::ContainerElevation;
        p.contentPadding = o.contentPadding;
        p.windowInsets = o.windowInsets.value_or(BottomAppBarDefaults::windowInsets());
        p.scrollBehavior = o.scrollBehavior;
        BottomAppBarLayout(modifier, p, std::move(content));
    }

}
