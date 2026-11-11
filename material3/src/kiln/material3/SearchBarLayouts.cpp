// The search bars themselves (SearchBar.kt): collapsed, in an app bar, expanded to full screen
// or docked, and the expanded-flag forms. State, behaviours and input fields: SearchBar.cpp.

#include "kiln/material3/SearchBar.h"

#include "kiln/animation/Easing.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Back.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/Divider.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/SearchBarInternal.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;
    using internal::Lerp;

    namespace {
        constexpr Dp SearchBarCornerRadius{ 28 };   // InputFieldHeight / 2

        rt::Task<> Collapse(std::shared_ptr<SearchBarState> s) { co_await s->AnimateToCollapsed(); }
        rt::Task<> RequestFocusTask(std::shared_ptr<ui::FocusRequester> f) {
            f->RequestFocus();
            co_return;
        }
        rt::Task<> HideKeyboardTask(ui::Owner* owner) {
            // The bar's window closes with it; the keyboard goes first instead of vanishing.
            if (owner) owner->HideSoftKeyboard();
            co_return;
        }
        rt::Task<> NothingTask() { co_return; }
        rt::Task<> HoldBack(BackProgress& events) {
            while (co_await events.Next()) {}
        }

        // asPaddingValues padding: the insets read when measuring, as plain padding (no
        // consumption: the bar consumed them already).
        ui::Modifier InsetsAsPadding(ui::Modifier m, std::function<WindowInsets()> insets, ui::Owner* owner) {
            return LayoutMod(std::move(m), [insets, owner](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
                const InsetsContext ctx{ owner ? &owner->Insets() : nullptr, s, s.Direction() };
                const WindowInsets w = insets();
                const int l = w.Left(ctx), t = w.Top(ctx), r = w.Right(ctx), b = w.Bottom(ctx);
                ui::Placeable& p = measurable.Measure(c.Offset(-(l + r), -(t + b)));
                return s.Layout(c.ConstrainWidth(p.Width() + l + r), c.ConstrainHeight(p.Height() + t + b), [&p, l, t](ui::PlacementScope& place) {
                    place.Place(p, l, t);
                });
            });
        }

        // The collapsed bar records where it sits (collapsedCoords).
        ui::Modifier RecordCollapsedBounds(ui::Modifier m, const std::shared_ptr<SearchBarState>& state) {
            auto& c = *rt::Composition::Current();
            c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(state.get())), [state]() -> std::function<void()> {
                return [state] { state->collapsedPlaced.Set(false); };
            });
            return OnGloballyPositioned(std::move(m), [state](const LayoutCoordinates& coords) {
                const ui::Offset p = coords.PositionInRoot();
                const ui::IntSize size = coords.GetSize();
                const SearchBarBounds r{ p.x, p.y, p.x + float(size.width), p.y + float(size.height) };
                if (!(state->collapsedBounds.Peek() == r)) state->collapsedBounds.Set(r);
                if (!state->collapsedPlaced.Peek()) state->collapsedPlaced.Set(true);
            });
        }

        // SearchBarImpl (the collapsed bar): the input field on its container, with no soft
        // keyboard of its own (the expanded bar's field takes the input).
        void CollapsedSearchBar(const std::shared_ptr<SearchBarState>& state, std::function<void()> inputField, const ui::Modifier& modifier, ShapePtr shape,
                                Color container, Color content, Dp tonal, Dp shadow) {
            const ui::Modifier m = RecordCollapsedBounds(modifier, state);
            BlockPlatformTextInput([&] {
                SurfaceOptions o;
                o.shape = std::move(shape);
                o.color = container;
                o.contentColor = content;
                o.tonalElevation = tonal;
                o.shadowElevation = shadow;
                Surface(m, o, std::move(inputField));
            });
        }
    }

    void SearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier, const SearchBarOptions& o) {
        const SearchBarColors colors = o.colors.value_or(SearchBarDefaults::colors());
        CollapsedSearchBar(state, std::move(inputField), modifier, o.shape ? o.shape : SearchBarDefaults::inputFieldShape(), colors.containerColor,
                           ContentColorFor(colors.containerColor), o.tonalElevation.value_or(SearchBarDefaults::TonalElevation),
                           o.shadowElevation.value_or(SearchBarDefaults::ShadowElevation));
    }

    void AppBarWithSearch(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier,
                          const AppBarWithSearchOptions& o) {
        const AppBarWithSearchColors colors = o.colors.value_or(SearchBarDefaults::appBarWithSearchColors());
        const bool transparent = colors.appBarContainerColor == Color::Transparent();
        const auto behavior = o.scrollBehavior;
        const float overlapped = behavior ? behavior->OverlappedFraction() : 0.0f;
        const float colorTransitionFraction = overlapped > 0.01f ? 1.0f : 0.0f;
        const Color appBarColor = AnimateColorAsState(colors.AppBarContainerColor(colorTransitionFraction), Value(MotionSchemeKeyTokens::DefaultEffects));
        const Color searchBarColor = colors.SearchBarContainerColor(colorTransitionFraction);
        const Color searchBarContent = ContentColorFor(colors.searchBarColors.containerColor);
        const Dp tonal = o.tonalElevation.value_or(SearchBarDefaults::TonalElevation), shadow = o.shadowElevation.value_or(SearchBarDefaults::ShadowElevation);

        ui::Modifier m = modifier;
        if (behavior) m = m.Then(behavior->SearchBarScrollBehaviorModifier());
        m = FillMax(std::move(m), Axis::Horizontal);
        m = WindowInsetsPadding(std::move(m), o.windowInsets.value_or(SearchBarDefaults::windowInsets()));
        m = Semantics(std::move(m), [](ui::SemanticsConfiguration& s) { s.isTraversalGroup = true; });
        SurfaceOptions surface;
        surface.color = appBarColor;
        surface.tonalElevation = transparent ? Dp(0) : tonal;
        surface.shadowElevation = transparent ? Dp(0) : shadow;
        const auto navigationIcon = o.navigationIcon, actions = o.actions;
        const ShapePtr shape = o.shape ? o.shape : SearchBarDefaults::inputFieldShape();
        const PaddingValues contentPadding = o.contentPadding;
        Surface(m, surface, [=] {
            Row(Padding({}, contentPadding), Arrangement::Start(), ui::AlignmentV::CenterVertically(), [=] {
                auto& c = *rt::Composition::Current();
                if (navigationIcon)
                    Box(Padding({}, PaddingValues{ internal::AppBarWithSearchHorizontalPadding, Dp(0), Dp(0), Dp(0) }), Alignment::TopStart(), false,
                        [&] { c.Provide(LocalContentColor, colors.appBarNavigationIconColor, navigationIcon); });
                // Hidden while the full-screen bar shows (but not as it starts to expand: no flicker).
                const bool visible = !state->expandsToFullScreen.Get() || !state->IsExpanded() || state->TargetValue() == SearchBarValue::Expanded;
                Box(Alpha(Weight({}, 1), visible ? 1.0f : 0.0f), Alignment::TopStart(), false, [=] {
                    ui::Modifier bar = Padding({}, internal::SearchBarAsTopBarPadding, internal::AppBarWithSearchVerticalPadding);
                    bar = SizeIn(std::move(bar), internal::SearchBarMinWidth, Dp::Unspecified(), internal::SearchBarMaxWidth, Dp::Unspecified());
                    bar = AlignInBox(std::move(bar), Alignment::Center());
                    CollapsedSearchBar(state, inputField, bar, shape, searchBarColor, searchBarContent, transparent ? tonal : Dp(0), transparent ? shadow : Dp(0));
                });
                if (actions)
                    Box(Padding({}, PaddingValues{ Dp(0), Dp(0), internal::AppBarWithSearchHorizontalPadding, Dp(0) }), Alignment::TopStart(), false, [&] {
                        c.Provide(LocalContentColor, colors.appBarActionIconColor,
                                  [&] { Row({}, Arrangement::End(), ui::AlignmentV::CenterVertically(), actions); });
                    });
            });
        });
    }

    void TopSearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier, const SearchBarOptions& o,
                      std::optional<WindowInsets> windowInsets, std::shared_ptr<SearchBarScrollBehavior> scrollBehavior) {
        AppBarWithSearchOptions a;
        AppBarWithSearchColors colors = SearchBarDefaults::appBarWithSearchColors();
        colors.searchBarColors = o.colors.value_or(SearchBarDefaults::colors());
        colors.scrolledSearchBarContainerColor = Color::Unspecified();
        colors.appBarContainerColor = Color::Transparent();
        colors.scrolledAppBarContainerColor = Color::Unspecified();
        a.colors = colors;
        a.shape = o.shape;
        a.tonalElevation = o.tonalElevation;
        a.shadowElevation = o.shadowElevation;
        a.windowInsets = windowInsets;
        a.scrollBehavior = std::move(scrollBehavior);
        AppBarWithSearch(std::move(state), std::move(inputField), modifier, a);
    }

    // --- expanded, full screen ------------------------------------------------------------------

    namespace {
        // BackEventProgress, as BasicEdgeToEdgeDialog's PredictiveBackState holds it.
        struct BackInfo {
            enum class Kind : std::uint8_t { NotRunning, InProgress, Completed } kind = Kind::NotRunning;
            float touchX = 0, touchY = 0, progress = 0;
            ui::SwipeEdge swipeEdge = ui::SwipeEdge::None;
            bool operator==(const BackInfo&) const = default;
        };
        using BackState = rt::State<BackInfo>;

        // PredictiveBackStateHandler: the gesture's progress into the state; completing it
        // dismisses.
        rt::Task<> BackStateHandler(BackProgress& events, std::shared_ptr<BackState> st, std::function<void()> onBack) {
            if (st->Peek().kind == BackInfo::Kind::Completed) {
                // Already dismissing (Compose disables the handler).
                co_await HoldBack(events);
                co_return;
            }
            struct OnCancel {
                std::shared_ptr<BackState> st;
                bool done = false;
                ~OnCancel() { if (!done) st->Set(BackInfo{}); }
            } guard{ st };
            while (auto e = co_await events.Next()) st->Set(BackInfo{ BackInfo::Kind::InProgress, e->touchX, e->touchY, e->progress, e->swipeEdge });
            guard.done = true;
            st->Set(BackInfo{ BackInfo::Kind::Completed });
            if (onBack) onBack();
        }

        using ExpandedContent = std::function<void(std::shared_ptr<ui::FocusRequester>, std::shared_ptr<BackState>)>;

        // ExpandedFullScreenSearchBarImpl: while expanded, an edge to edge window with the bar.
        void ExpandedFullScreenImpl(const std::shared_ptr<SearchBarState>& state, DialogProperties properties, ExpandedContent content) {
            if (!state->IsExpanded()) return;
            auto& c = *rt::Composition::Current();
            const auto scope = internal::RememberCoroutineScope();
            const auto back = c.Remember<std::shared_ptr<BackState>>([] { return std::make_shared<BackState>(BackInfo{}); });
            const auto onDismiss = [scope, state] { scope->Launch(Collapse(state)); };
            const bool dismissOnBack = properties.dismissOnBackPress;
            // BasicEdgeToEdgeDialog: the window spans its parent, draws no dim and pads nothing.
            properties.usePlatformDefaultWidth = false;
            properties.decorFitsSystemWindows = false;
            properties.scrimAlpha = 0;
            std::function<rt::Task<>(BackProgress&)> onBack;
            if (dismissOnBack) onBack = [back, onDismiss](BackProgress& events) { return BackStateHandler(events, back, onDismiss); };
            else onBack = [](BackProgress& events) { return HoldBack(events); };
            Dialog(onDismiss, properties, onBack, [=] {
                auto& cc = *rt::Composition::Current();
                const auto focus = cc.Remember<std::shared_ptr<ui::FocusRequester>>([] { return std::make_shared<ui::FocusRequester>(); });
                ui::Modifier box = OnKeyEvent({}, [dismissOnBack, onDismiss](const ui::KeyEvent& e) {
                    // The dialog window's onKeyUp: Escape dismisses as Back does.
                    if (dismissOnBack && e.type == ui::KeyEventType::KeyUp && e.keyCode == ui::keys::Escape) {
                        onDismiss();
                        return true;
                    }
                    return false;
                });
                Box(box, Alignment::TopStart(), false, [&] { content(focus, back); });
                // The field takes focus on the first expansion only.
                cc.LaunchedEffect(rt::Keys(0), [focus]() -> rt::Task<> { return RequestFocusTask(focus); });
                const bool collapsing = state->TargetValue() == SearchBarValue::Collapsed;
                ui::Owner* owner = cc.Read(ui::LocalOwner);
                cc.LaunchedEffect(rt::Keys(collapsing), [collapsing, owner]() -> rt::Task<> {
                    if (collapsing) return HideKeyboardTask(owner);
                    return NothingTask();
                });
            });
        }

        // The predictive back gesture's first and latest in-progress events.
        struct BackEvents {
            rt::State<std::optional<BackInfo>> first{ std::nullopt }, last{ std::nullopt };
            void Track(const BackInfo& e) {
                switch (e.kind) {
                    case BackInfo::Kind::InProgress:
                        if (!first.Peek()) first.Set(e);
                        last.Set(e);
                        break;
                    case BackInfo::Kind::NotRunning:
                        if (first.Peek()) first.Set(std::nullopt);
                        if (last.Peek()) last.Set(std::nullopt);
                        break;
                    case BackInfo::Kind::Completed: break;
                }
            }
        };
        float Transform(const std::optional<BackInfo>& e) { return e ? internal::PredictiveBackTransform(e->progress) : 0.0f; }

        struct FullScreenParams {
            std::shared_ptr<SearchBarState> state;
            std::shared_ptr<BackState> back;
            std::function<void()> inputField;
            PaddingValues inputFieldPadding;
            ui::Modifier modifier;
            ShapePtr collapsedShape;
            Color containerColor, contentColor;
            Dp tonalElevation, shadowElevation;
            WindowInsets windowInsets;
            bool isContained = false;
            std::function<void()> content;
        };

        struct FullScreenPolicy final : ui::MeasurePolicy {
            std::shared_ptr<SearchBarState> state;
            std::shared_ptr<BackEvents> events;
            std::shared_ptr<rt::State<WindowInsets>> unconsumed;
            ui::Owner* owner = nullptr;
            PaddingValues inputFieldPadding;
            bool isContained = false;

            // children: the surface, the input field, the content.
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                const InsetsContext ctx{ owner ? &owner->Insets() : nullptr, s, s.Direction() };
                const auto last = events->last.Get();
                const float predictiveBackProgress = Transform(last);
                const float progress = state->Progress();
                const SearchBarBounds bounds = state->collapsedBounds.Get();
                const int boundsLeft = int(std::lround(bounds.left)), boundsTop = int(std::lround(bounds.top));
                const int boundsRight = int(std::lround(bounds.right)), boundsW = int(std::lround(bounds.Width())), boundsH = int(std::lround(bounds.Height()));
                const int collapsedWidth = boundsW != 0 ? boundsW : s.RoundToPx(internal::SearchBarMinWidth);
                const int collapsedHeight = boundsH != 0 ? boundsH : s.RoundToPx(SearchBarDefaults::InputFieldHeight);
                const int endWidthBack = std::max(int(std::lround(float(c.maxWidth) * internal::SearchBarPredictiveBackMinScale)), collapsedWidth);
                const int endHeightBack = std::max(int(std::lround(float(c.maxHeight) * internal::SearchBarPredictiveBackMinScale)), collapsedHeight);
                const int endWidth = Lerp(c.maxWidth, endWidthBack, predictiveBackProgress);
                const int endHeight = Lerp(c.maxHeight, endHeightBack, predictiveBackProgress);
                int width, height;
                if (isContained) {
                    width = endWidth;
                    height = endHeight;
                } else {
                    width = c.ConstrainWidth(Lerp(collapsedWidth, endWidth, progress));
                    height = c.ConstrainHeight(Lerp(collapsedHeight, endHeight, progress));
                }
                ui::Placeable& surface = children[0]->Measure(ui::Constraints::Fixed(width, height));
                const bool rtl = s.Direction() == ui::LayoutDirection::Rtl;
                const int startPadding = s.RoundToPx(rtl && inputFieldPadding.rtlAware ? inputFieldPadding.end : inputFieldPadding.start);
                const int endPadding = s.RoundToPx(rtl && inputFieldPadding.rtlAware ? inputFieldPadding.start : inputFieldPadding.end);
                const int paddedInputFieldWidth = Lerp(collapsedWidth, width - startPadding - endPadding, state->RawProgress());
                ui::Placeable& input = children[1]->Measure(ui::Constraints::Fixed(std::max(0, paddedInputFieldWidth), collapsedHeight));
                const int topPadding = unconsumed->Get().Top(ctx) + s.RoundToPx(isContained ? internal::AppBarWithSearchVerticalPadding : internal::SearchBarVerticalPadding);
                const int animatedTopPadding = Lerp(0, topPadding, std::min(progress, 1 - predictiveBackProgress));
                const int verticalPadding = s.RoundToPx(internal::SearchBarVerticalPadding);
                const int bottomPadding = isContained ? verticalPadding : Lerp(0, verticalPadding, progress);
                const int paddedInputFieldHeight = input.Height() + animatedTopPadding + bottomPadding;
                ui::Placeable& content = children[2]->Measure(ui::Constraints{ width, width, 0, std::max(0, height - paddedInputFieldHeight) });

                const int minMargin = s.RoundToPx(internal::SearchBarPredictiveBackMinMargin);
                const int maxOffsetY = s.RoundToPx(internal::SearchBarPredictiveBackMaxOffsetY);
                const auto first = events->first.Get();
                const float rawProgress = state->RawProgress();
                const int maxW = c.maxWidth, maxH = c.maxHeight;
                const bool contained = isContained;
                return s.Layout(maxW, maxH, [=, &surface, &input, &content](ui::PlacementScope& place) {
                    const auto endOffsetX = [&](const BackInfo& e) {
                        int x = e.swipeEdge == ui::SwipeEdge::Left ? maxW - minMargin - endWidthBack : minMargin;
                        x = std::max(x, boundsRight - endWidthBack);
                        return std::min(x, boundsLeft);
                    };
                    const auto endOffsetY = [&](const BackInfo& e) {
                        if (!first) return 0;
                        const float absoluteDeltaY = e.touchY - first->touchY;
                        const float relativeDeltaY = std::abs(absoluteDeltaY) / float(maxH);
                        const int available = std::max(0, (maxH - endHeightBack) / 2 - minMargin);
                        const int total = std::min(available, maxOffsetY);
                        const int interpolated = Lerp(0, total, relativeDeltaY);
                        const int sign = absoluteDeltaY > 0 ? 1 : absoluteDeltaY < 0 ? -1 : 0;
                        return std::min(interpolated * sign + topPadding, boundsTop);
                    };
                    const int predictiveBackOffsetX = Lerp(0, last ? endOffsetX(*last) : 0, predictiveBackProgress);
                    const int offsetX = contained ? predictiveBackOffsetX : Lerp(boundsLeft, predictiveBackOffsetX, progress);
                    const float currentCenterX = (bounds.left + bounds.right) / 2 + ((float(offsetX) + float(width) / 2) - (bounds.left + bounds.right) / 2) * rawProgress;
                    const int offsetY = Lerp(0, last ? endOffsetY(*last) : 0, predictiveBackProgress);
                    const int animatedOffsetY = Lerp(boundsTop, offsetY, progress);
                    place.Place(surface, offsetX, contained ? offsetY : animatedOffsetY);
                    place.Place(input, int(std::lround(currentCenterX - float(input.Width()) / 2)), animatedOffsetY + animatedTopPadding);
                    place.Place(content, offsetX, animatedOffsetY + animatedTopPadding + input.Height() + bottomPadding);
                });
            }
        };

        void FullScreenSearchBarLayout(const FullScreenParams& p) {
            auto& c = *rt::Composition::Current();
            const auto state = p.state;
            const auto events = c.Remember<std::shared_ptr<BackEvents>>([] { return std::make_shared<BackEvents>(); });
            events->Track(p.back->Get());
            const auto unconsumed = c.Remember<std::shared_ptr<rt::State<WindowInsets>>>([] { return std::make_shared<rt::State<WindowInsets>>(WindowInsets{}); });
            ui::Owner* owner = c.Read(ui::LocalOwner);
            const WindowInsets windowInsets = p.windowInsets;
            ui::Modifier m = OnConsumedWindowInsetsChanged(p.modifier, [unconsumed, windowInsets](const WindowInsets& consumed) {
                const WindowInsets next = windowInsets.Exclude(consumed);
                if (!(unconsumed->Peek() == next)) unconsumed->Set(next);
            });
            m = ConsumeWindowInsets(std::move(m), windowInsets);
            const auto nonTop = [unconsumed] { return unconsumed->Get().Only(WindowInsetsSides::Horizontal() + WindowInsetsSides::Bottom()); };
            const auto horizontal = [unconsumed] { return unconsumed->Get().Only(WindowInsetsSides::Horizontal()); };
            const auto policy = c.Remember<std::shared_ptr<FullScreenPolicy>>([] { return std::make_shared<FullScreenPolicy>(); });
            policy->state = state;
            policy->events = events;
            policy->unconsumed = unconsumed;
            policy->owner = owner;
            policy->inputFieldPadding = p.inputFieldPadding;
            policy->isContained = p.isContained;
            const FullScreenParams params = p;
            Layout(m, policy, [=] {
                auto& cc = *rt::Composition::Current();
                // The surface: its corners round off as it shrinks back into the collapsed bar.
                cc.Child([=] {
                    const ShapePtr fullScreen = SearchBarDefaults::fullScreenShape();
                    ShapePtr shape;
                    if (params.collapsedShape == CircleShape() && fullScreen == RectangleShape()) {
                        // The shape animates only between the default spec values.
                        const float fraction = std::max(1 - state->Progress(), Transform(events->last.Get()));
                        const float radius = SearchBarCornerRadius.value * fraction;
                        shape = radius < 1e-3f ? RectangleShape() : RoundedCorner(Dp(radius));
                    } else {
                        shape = state->Progress() < 0.5f ? params.collapsedShape : fullScreen;
                    }
                    SurfaceOptions o;
                    o.shape = shape;
                    o.color = params.containerColor;
                    o.contentColor = params.contentColor;
                    o.tonalElevation = params.tonalElevation;
                    o.shadowElevation = params.shadowElevation;
                    const bool contained = params.isContained;
                    Surface(GraphicsLayer({}, [state, contained](GraphicsLayerParams& l) { if (contained) l.alpha = state->Progress(); }), o, [] {});
                });
                Box(Clip(InsetsAsPadding({}, horizontal, owner), params.collapsedShape), Alignment::TopStart(), true, params.inputField);
                ui::Modifier col = GraphicsLayer({}, [state, contained = params.isContained](GraphicsLayerParams& l) {
                    l.alpha = contained ? state->ContentProgress() : state->Progress();
                });
                col = InsetsAsPadding(std::move(col), nonTop, owner);
                Column(col, Arrangement::Top(), ui::AlignmentH::Start(), params.content);
            });
        }

        void FullScreenBar(const std::shared_ptr<SearchBarState>& state, std::function<void()> inputField, const ui::Modifier& modifier,
                           const ExpandedFullScreenSearchBarOptions& o, std::function<void()> content, bool contained) {
            if (!state->expandsToFullScreen.Peek()) state->expandsToFullScreen.Set(true);
            const SearchBarColors colors = o.colors.value_or(contained ? SearchBarDefaults::containedColors(*state) : SearchBarDefaults::colors());
            FullScreenParams p;
            p.state = state;
            p.inputFieldPadding = contained ? PaddingValues{ internal::FullScreenExpandedHorizontalPadding, Dp(0), internal::FullScreenExpandedHorizontalPadding, Dp(0) }
                                            : PaddingValues{};
            p.modifier = modifier;
            p.collapsedShape = o.collapsedShape ? o.collapsedShape : SearchBarDefaults::inputFieldShape();
            p.containerColor = colors.containerColor;
            p.contentColor = ContentColorFor(colors.containerColor);
            p.tonalElevation = o.tonalElevation.value_or(SearchBarDefaults::TonalElevation);
            p.shadowElevation = o.shadowElevation.value_or(SearchBarDefaults::ShadowElevation);
            p.isContained = contained;
            const Color divider = colors.dividerColor;
            if (contained) p.content = std::move(content);
            else p.content = [divider, content] {
                HorizontalDivider({}, DividerDefaults::Thickness, divider);
                content();
            };
            const auto windowInsets = o.windowInsets;
            ExpandedFullScreenImpl(state, o.properties, [p, inputField, windowInsets](std::shared_ptr<ui::FocusRequester> focus, std::shared_ptr<BackState> back) {
                FullScreenParams q = p;
                q.back = back;
                q.windowInsets = windowInsets ? windowInsets() : SearchBarDefaults::fullScreenWindowInsets();
                q.inputField = [focus, inputField] { Box(FocusRequesterModifier({}, *focus), Alignment::TopStart(), true, inputField); };
                FullScreenSearchBarLayout(q);
            });
        }
    }

    void ExpandedFullScreenSearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier,
                                     const ExpandedFullScreenSearchBarOptions& options, std::function<void()> content) {
        FullScreenBar(state, std::move(inputField), modifier, options, std::move(content), false);
    }

    void ExpandedFullScreenContainedSearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier,
                                              const ExpandedFullScreenSearchBarOptions& options, std::function<void()> content) {
        FullScreenBar(state, std::move(inputField), modifier, options, std::move(content), true);
    }

    // --- expanded, docked -----------------------------------------------------------------------

    namespace {
        // At the collapsed bar's corner (or the window's, under a scrim that covers it).
        class DockedPosition final : public PopupPositionProvider {
        public:
            DockedPosition(std::shared_ptr<SearchBarState> state, bool hasScrim) : m_State(std::move(state)), m_HasScrim(hasScrim) {}
            ui::IntOffset CalculatePosition(ui::IntRect, ui::IntSize, ui::LayoutDirection, ui::IntSize) const override {
                if (m_HasScrim) return {};
                const SearchBarBounds b = m_State->collapsedBounds.Get();
                return { int(std::lround(b.left)), int(std::lround(b.top)) };
            }

        private:
            std::shared_ptr<SearchBarState> m_State;
            bool m_HasScrim;
        };

        struct DockedPolicy final : MultiContentMeasurePolicy {
            std::shared_ptr<SearchBarState> state;
            Dp minHeight, maxHeight;
            ui::MeasureResult Measure(ui::MeasureScope& s, const std::vector<std::vector<ui::Measurable*>>& contents, const ui::Constraints& base) override {
                const int maxHeightPx = s.RoundToPx(maxHeight);
                const int collapsedHeight = int(std::lround(state->collapsedBounds.Get().Height()));
                const int constraintMaxHeight = Lerp(collapsedHeight, maxHeightPx, std::max(0.0f, state->RawProgress()));
                const auto collapsedWidth = state->CollapsedWidth();
                const ui::Constraints constraints =
                    base.Constrain(ui::Constraints{ 0, collapsedWidth.value_or(base.maxWidth), std::min(s.RoundToPx(minHeight), constraintMaxHeight), constraintMaxHeight });
                ui::Constraints loose = constraints;
                loose.minWidth = loose.minHeight = 0;
                std::vector<ui::Placeable*> inputs, contentPlaceables;
                int inputWidth = 0, inputHeight = 0;
                for (ui::Measurable* m : contents[0]) {
                    ui::Placeable& p = m->Measure(loose);
                    inputs.push_back(&p);
                    inputWidth = std::max(inputWidth, p.Width());
                    inputHeight = std::max(inputHeight, p.Height());
                }
                ui::Constraints contentConstraints = loose.Offset(0, -inputHeight);
                contentConstraints.maxWidth = inputWidth;
                int contentWidth = 0, contentHeight = 0;
                for (ui::Measurable* m : contents[1]) {
                    ui::Placeable& p = m->Measure(contentConstraints);
                    contentPlaceables.push_back(&p);
                    contentWidth = std::max(contentWidth, p.Width());
                    contentHeight = std::max(contentHeight, p.Height());
                }
                const int width = std::max(inputWidth, contentWidth);
                const int height = inputHeight + contentHeight;
                return s.Layout(constraints.ConstrainWidth(width), constraints.ConstrainHeight(height), [inputs, contentPlaceables, inputHeight](ui::PlacementScope& place) {
                    for (ui::Placeable* p : inputs) place.Place(*p, 0, 0);
                    for (ui::Placeable* p : contentPlaceables) place.Place(*p, 0, inputHeight);
                });
            }
        };

        // DockedSearchBarLayoutImpl: the field and the results on one surface that grows from the
        // collapsed bar's height to its maximum with the progress; Back collapses it.
        void DockedSearchBarLayoutImpl(const std::shared_ptr<SearchBarState>& state, ShapePtr shape, std::function<void()> inputField, const ui::Modifier& modifier,
                                       Color containerColor, Dp tonal, Dp shadow, bool hasGap, std::function<void()> content) {
            auto& c = *rt::Composition::Current();
            const auto scope = internal::RememberCoroutineScope();
            BackHandler(state->IsExpanded(), [scope, state] { scope->Launch(Collapse(state)); });
            SurfaceOptions o;
            o.shape = std::move(shape);
            o.color = containerColor;
            o.contentColor = ContentColorFor(containerColor);
            o.tonalElevation = tonal;
            o.shadowElevation = shadow;
            const Dp windowHeight = internal::WindowContainerHeight();
            const float ratio = hasGap ? internal::DockedExpandedWithGapTableMaxHeightScreenRatio : internal::DockedExpandedTableMaxHeightScreenRatio;
            const Dp maxHeight{ windowHeight.value * ratio };
            const Dp minHeight{ std::min(internal::DockedExpandedTableMinHeight.value, maxHeight.value) };
            const auto policy = c.Remember<std::shared_ptr<DockedPolicy>>([] { return std::make_shared<DockedPolicy>(); });
            policy->state = state;
            policy->minHeight = minHeight;
            policy->maxHeight = maxHeight;
            Surface(ImePadding(modifier), o, [=] { Layout({}, { inputField, content }, policy); });
        }

        void DockedSearchBarLayout(const std::shared_ptr<SearchBarState>& state, std::function<void()> inputField, const ui::Modifier& modifier, ShapePtr searchBarShape,
                                   ShapePtr dropdownShape, std::optional<Dp> gap, Color containerColor, Color dividerColor, Dp tonal, Dp shadow,
                                   std::function<void()> content) {
            const bool hasDropdown = dropdownShape != nullptr;
            std::function<void()> field = inputField;
            if (hasDropdown)
                field = [=] { Box(Clip(Background({}, containerColor, searchBarShape), searchBarShape), Alignment::TopStart(), false, inputField); };
            std::function<void()> results;
            if (hasDropdown) {
                results = [=] {
                    const bool sufficientProgress = state->Progress() > 0.1f;
                    ui::Modifier m = Padding({}, PaddingValues{ Dp(0), gap.value_or(Dp(0)), Dp(0), Dp(0) });
                    m = Background(std::move(m), containerColor, dropdownShape);
                    m = Clip(std::move(m), dropdownShape);
                    m = GraphicsLayer(std::move(m), [state](GraphicsLayerParams& l) { l.alpha = state->ContentProgress(); });
                    // Half its height above, sliding in with the bar's own motion.
                    const anim::SpecPtr in = state->animationSpecForExpand ? state->animationSpecForExpand : anim::Snap();
                    const anim::SpecPtr out = state->animationSpecForCollapse ? state->animationSpecForCollapse : anim::Snap();
                    AnimatedVisibility(sufficientProgress && state->TargetValue() == SearchBarValue::Expanded, m,
                                       [content] { Column({}, Arrangement::Top(), ui::AlignmentH::Start(), content); },
                                       SlideIn([](ui::IntSize s) { return ui::IntOffset{ 0, int(std::lround(-float(s.height) / 2)) }; }, in),
                                       SlideOut([](ui::IntSize s) { return ui::IntOffset{ 0, int(std::lround(-float(s.height) / 2)) }; }, out));
                };
            } else {
                results = [=] {
                    Column({}, Arrangement::Top(), ui::AlignmentH::Start(), [=] {
                        HorizontalDivider({}, DividerDefaults::Thickness, dividerColor);
                        content();
                    });
                };
            }
            DockedSearchBarLayoutImpl(state, hasDropdown ? RectangleShape() : searchBarShape, field, modifier, hasDropdown ? Color::Transparent() : containerColor,
                                      tonal, shadow, gap.has_value(), results);
        }

        void DockedBar(const std::shared_ptr<SearchBarState>& state, std::function<void()> inputField, const ui::Modifier& modifier, const ExpandedDockedSearchBarOptions& o,
                       std::function<void()> content, bool withGap) {
            if (!state->IsExpanded()) return;
            auto& c = *rt::Composition::Current();
            const Color scrimColor = withGap ? o.dropdownScrimColor.value_or(SearchBarDefaults::dockedDropdownScrimColor()) : Color::Unspecified();
            const bool hasScrim = scrimColor.Specified() && scrimColor != Color::Transparent();
            const auto scope = internal::RememberCoroutineScope();
            const auto onDismiss = [scope, state] { scope->Launch(Collapse(state)); };
            const auto position = c.Remember<std::shared_ptr<DockedPosition>>([&] { return std::make_shared<DockedPosition>(state, hasScrim); },
                                                                              rt::Keys(reinterpret_cast<std::uintptr_t>(state.get()), hasScrim));
            const SearchBarColors colors = o.colors.value_or(SearchBarDefaults::colors());
            const ShapePtr shape = o.shape ? o.shape : SearchBarDefaults::dockedShape();
            const ShapePtr dropdownShape = withGap ? (o.dropdownShape ? o.dropdownShape : SearchBarDefaults::dockedDropdownShape()) : nullptr;
            const std::optional<Dp> gap = withGap ? std::optional<Dp>(o.dropdownGapSize.value_or(SearchBarDefaults::dockedDropdownGapSize)) : std::nullopt;
            const Dp tonal = o.tonalElevation.value_or(SearchBarDefaults::TonalElevation), shadow = o.shadowElevation.value_or(SearchBarDefaults::ShadowElevation);
            Popup(position, onDismiss, o.properties, [=] {
                auto& cc = *rt::Composition::Current();
                const auto focus = cc.Remember<std::shared_ptr<ui::FocusRequester>>([] { return std::make_shared<ui::FocusRequester>(); });
                const auto layout = [=] {
                    DockedSearchBarLayout(state, [focus, inputField] { Box(FocusRequesterModifier({}, *focus), Alignment::TopStart(), true, inputField); },
                                          modifier, shape, dropdownShape, gap, colors.containerColor, colors.dividerColor, tonal, shadow, content);
                };
                if (hasScrim) {
                    ui::Modifier m = FillMax({}, Axis::Both);
                    m = DrawBehind(std::move(m), [scrimColor, state](DrawScope& d) { d.DrawRect(scrimColor.WithAlpha(scrimColor.Alpha() * state->Progress()), {}, d.GetSize()); });
                    m = Clickable(std::move(m), onDismiss);
                    m = OffsetMod(std::move(m), [state](const ui::Density&) {
                        const SearchBarBounds b = state->collapsedBounds.Get();
                        return ui::IntOffset{ int(std::lround(b.left)), int(std::lround(b.top)) };
                    });
                    Box(m, Alignment::TopStart(), false, layout);
                } else {
                    layout();
                }
                cc.LaunchedEffect(rt::Keys(0), [focus]() -> rt::Task<> { return RequestFocusTask(focus); });
                const bool collapsing = state->TargetValue() == SearchBarValue::Collapsed;
                ui::Owner* owner = cc.Read(ui::LocalOwner);
                cc.LaunchedEffect(rt::Keys(collapsing), [collapsing, owner]() -> rt::Task<> {
                    if (collapsing) return HideKeyboardTask(owner);
                    return NothingTask();
                });
            });
        }
    }

    void ExpandedDockedSearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier,
                                 const ExpandedDockedSearchBarOptions& options, std::function<void()> content) {
        DockedBar(state, std::move(inputField), modifier, options, std::move(content), false);
    }

    void ExpandedDockedSearchBarWithGap(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier,
                                        const ExpandedDockedSearchBarOptions& options, std::function<void()> content) {
        DockedBar(state, std::move(inputField), modifier, options, std::move(content), true);
    }

    // --- the expanded-flag search bars ----------------------------------------------------------

    namespace {
        // What the layout uses of a back event.
        struct BackPoint {
            float touchY = 0;
            ui::SwipeEdge swipeEdge = ui::SwipeEdge::None;
            bool operator==(const BackPoint&) const = default;
        };
        struct LegacyBack {
            std::shared_ptr<anim::Animatable> progress;
            rt::State<float> finalBackProgress{ NAN };
            rt::State<std::optional<BackPoint>> first{ std::nullopt }, current{ std::nullopt };
            void Reset() {
                finalBackProgress.Set(NAN);
                first.Set(std::nullopt);
                current.Set(std::nullopt);
            }
        };

        rt::Task<> AnimateExpanded(std::shared_ptr<LegacyBack> b, bool expanded) {
            const float v = b->progress->Float();
            const bool inProgress = v > 0 && v < 1;
            const anim::SpecPtr spec = inProgress ? internal::AnimationPredictiveBackExitFloatSpec()
                                     : expanded   ? internal::AnimationEnterFloatSpec()
                                                  : internal::AnimationExitFloatSpec();
            const float target = expanded ? 1.0f : 0.0f;
            if (v != target) co_await b->progress->AnimateTo(anim::Vec{ target, 0, 0, 0 }, spec);
            if (!expanded) b->Reset();
        }
        rt::Task<> SpringBack(std::shared_ptr<LegacyBack> b) {
            co_await b->progress->AnimateTo(anim::Vec{ 1, 0, 0, 0 }, internal::AnimationPredictiveBackExitFloatSpec());
            b->Reset();
        }
        // The bar follows the gesture; completing it collapses, cancelling springs back.
        rt::Task<> LegacyPredictiveBack(BackProgress& events, std::shared_ptr<LegacyBack> b, std::shared_ptr<internal::CoroutineScope> scope,
                                        std::function<void(bool)> onExpandedChange) {
            struct OnCancel {
                std::shared_ptr<LegacyBack> b;
                std::shared_ptr<internal::CoroutineScope> scope;
                bool done = false;
                ~OnCancel() { if (!done) scope->Launch(SpringBack(b)); }
            } guard{ b, scope };
            b->finalBackProgress.Set(NAN);
            while (auto e = co_await events.Next()) {
                const BackPoint point{ e->touchY, e->swipeEdge };
                if (!b->first.Peek()) b->first.Set(point);
                b->current.Set(point);
                b->progress->SnapTo(1 - internal::PredictiveBackTransform(e->progress));
            }
            guard.done = true;
            b->finalBackProgress.Set(b->progress->Float());
            if (onExpandedChange) onExpandedChange(false);
        }

        float PredictiveBackMultiplier(const std::optional<BackPoint>& current, float progress, float finalBackProgress) {
            if (!current) return 0;                         // not in predictive back
            if (std::isnan(finalBackProgress)) return 1;    // still swiping
            if (finalBackProgress <= 0) return 0;
            return progress / finalBackProgress;            // released
        }

        struct LegacyLayoutPolicy final : ui::MeasurePolicy {
            std::shared_ptr<LegacyBack> back;
            std::shared_ptr<rt::State<WindowInsets>> unconsumed;
            ui::Owner* owner = nullptr;
            // children: the surface, the input field, the content (while shown).
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                const float progress = back->progress->Float();
                const InsetsContext ctx{ owner ? &owner->Insets() : nullptr, s, s.Direction() };
                const int topPadding = unconsumed->Get().Top(ctx) + s.RoundToPx(internal::SearchBarVerticalPadding);
                const int bottomPadding = s.RoundToPx(internal::SearchBarVerticalPadding);
                ui::Measurable& inputM = *children[1];
                const int defaultStartWidth = c.ConstrainWidth(inputM.MaxIntrinsicWidth(c.maxHeight));
                const int defaultStartHeight = c.ConstrainHeight(inputM.MinIntrinsicHeight(c.maxWidth));
                const int backStartWidth = int(std::lround(float(c.maxWidth) * internal::SearchBarPredictiveBackMinScale));
                const int backStartHeight = int(std::lround(float(c.maxHeight) * internal::SearchBarPredictiveBackMinScale));
                const auto current = back->current.Get();
                const auto first = back->first.Get();
                const float multiplier = PredictiveBackMultiplier(current, progress, back->finalBackProgress.Get());
                const int startWidth = Lerp(defaultStartWidth, backStartWidth, multiplier);
                const int startHeight = Lerp(topPadding + defaultStartHeight, backStartHeight, multiplier);
                const int maxWidth = c.maxWidth, maxHeight = c.maxHeight;
                const int minWidth = Lerp(startWidth, maxWidth, progress);
                const int height = Lerp(startHeight, maxHeight, progress);
                const int animatedTopPadding = Lerp(topPadding, 0, progress);
                const int animatedBottomPadding = Lerp(0, bottomPadding, progress);
                ui::Placeable& input = inputM.Measure(ui::Constraints{ minWidth, maxWidth, defaultStartHeight, defaultStartHeight });
                const int width = input.Width();
                ui::Placeable& surface = children[0]->Measure(ui::Constraints::Fixed(width, height - animatedTopPadding));
                ui::Placeable* content = nullptr;
                if (children.size() > 2)
                    content = &children[2]->Measure(ui::Constraints{ width, width, 0,
                                                                     c.HasBoundedHeight() ? std::max(0, c.maxHeight - (topPadding + defaultStartHeight + bottomPadding))
                                                                                          : c.maxHeight });
                const int minMargin = s.RoundToPx(internal::SearchBarPredictiveBackMinMargin);
                const int maxOffsetY = s.RoundToPx(internal::SearchBarPredictiveBackMaxOffsetY);
                const ui::LayoutDirection dir = s.Direction();
                return s.Layout(width, height, [=, &surface, &input](ui::PlacementScope& place) {
                    int offsetX = 0, offsetY = 0;
                    if (current && multiplier != 0) {
                        const int directionMultiplier = current->swipeEdge == ui::SwipeEdge::Left ? 1 : -1;
                        const int rtlMultiplier = dir == ui::LayoutDirection::Ltr ? 1 : -1;
                        const float maxOffsetX = float(maxWidth) * internal::SearchBarPredictiveBackMaxOffsetXRatio - float(minMargin);
                        offsetX = int(std::lround(maxOffsetX * (1 - progress) * multiplier * float(directionMultiplier * rtlMultiplier)));
                        if (first) {
                            const int available = std::max(0, (maxHeight - height) / 2 - minMargin);
                            const int adjustedMax = std::min(available, maxOffsetY);
                            const float yDelta = current->touchY - first->touchY;
                            const float yProgress = std::abs(yDelta) / float(maxHeight);
                            const float sign = yDelta > 0 ? 1.0f : yDelta < 0 ? -1.0f : 0.0f;
                            offsetY = int(std::lround(float(Lerp(0, adjustedMax, yProgress)) * multiplier * sign));
                        }
                    }
                    place.PlaceRelative(surface, offsetX, offsetY + animatedTopPadding);
                    place.PlaceRelative(input, offsetX, offsetY + topPadding);
                    if (content) place.PlaceRelative(*content, offsetX, offsetY + topPadding + input.Height() + animatedBottomPadding);
                });
            }
        };
    }

    void SearchBar(std::function<void()> inputField, bool expanded, std::function<void(bool)> onExpandedChange, const ui::Modifier& modifier,
                   const ExpandableSearchBarOptions& o, std::function<void()> content) {
        auto& c = *rt::Composition::Current();
        const auto scope = internal::RememberCoroutineScope();
        const auto back = c.Remember<std::shared_ptr<LegacyBack>>([expanded] {
            auto b = std::make_shared<LegacyBack>();
            b->progress = std::make_shared<anim::Animatable>(expanded ? 1.0f : 0.0f);
            return b;
        });
        c.LaunchedEffect(rt::Keys(expanded), [back, expanded]() -> rt::Task<> { return AnimateExpanded(back, expanded); });
        PredictiveBackHandler(expanded, [back, scope, onExpandedChange](BackProgress& events) {
            return LegacyPredictiveBack(events, back, scope, onExpandedChange);
        });

        const SearchBarColors colors = o.colors.value_or(SearchBarDefaults::colors());
        const ShapePtr shape = o.shape ? o.shape : SearchBarDefaults::inputFieldShape();
        const Dp tonal = o.tonalElevation.value_or(SearchBarDefaults::TonalElevation), shadow = o.shadowElevation.value_or(SearchBarDefaults::ShadowElevation);
        const WindowInsets windowInsets = o.windowInsets.value_or(SearchBarDefaults::windowInsets());
        const auto unconsumed = c.Remember<std::shared_ptr<rt::State<WindowInsets>>>([] { return std::make_shared<rt::State<WindowInsets>>(WindowInsets{}); });
        ui::Owner* owner = c.Read(ui::LocalOwner);
        ui::Modifier m = ZIndex(modifier, 1);
        m = OnConsumedWindowInsetsChanged(std::move(m), [unconsumed, windowInsets](const WindowInsets& consumed) {
            const WindowInsets next = windowInsets.Exclude(consumed);
            if (!(unconsumed->Peek() == next)) unconsumed->Set(next);
        });
        m = ConsumeWindowInsets(std::move(m), windowInsets);
        const auto policy = c.Remember<std::shared_ptr<LegacyLayoutPolicy>>([] { return std::make_shared<LegacyLayoutPolicy>(); });
        policy->back = back;
        policy->unconsumed = unconsumed;
        policy->owner = owner;
        const bool showContent = back->progress->Float() > 0;
        Layout(m, policy, [=] {
            Box({}, Alignment::TopStart(), true, [=] {
                // The corners round off with the progress (only the default shape animates).
                const float progress = back->progress->Float();
                ShapePtr animated;
                if (shape == SearchBarDefaults::inputFieldShape()) animated = RoundedCorner(Dp(SearchBarCornerRadius.value * (1 - progress)));
                else if (progress == 1) animated = SearchBarDefaults::fullScreenShape();
                else animated = shape;
                SurfaceOptions so;
                so.shape = animated;
                so.color = colors.containerColor;
                so.contentColor = ContentColorFor(colors.containerColor);
                so.tonalElevation = tonal;
                so.shadowElevation = shadow;
                Surface({}, so, [] {});
            });
            Box({}, Alignment::TopStart(), true, inputField);
            if (showContent)
                Box({}, Alignment::TopStart(), true, [=] {
                    Column(GraphicsLayer({}, [back](GraphicsLayerParams& l) { l.alpha = back->progress->Float(); }), Arrangement::Top(), ui::AlignmentH::Start(),
                           [=] {
                               HorizontalDivider({}, DividerDefaults::Thickness, colors.dividerColor);
                               rt::Composition::Current()->Provide(LocalContentColor, ContentColorFor(colors.containerColor), content);
                           });
                });
        });
    }

    void DockedSearchBar(std::function<void()> inputField, bool expanded, std::function<void(bool)> onExpandedChange, const ui::Modifier& modifier,
                         const ExpandableSearchBarOptions& o, std::function<void()> content) {
        const SearchBarColors colors = o.colors.value_or(SearchBarDefaults::colors());
        SurfaceOptions so;
        so.shape = o.shape ? o.shape : SearchBarDefaults::dockedShape();
        so.color = colors.containerColor;
        so.contentColor = ContentColorFor(colors.containerColor);
        so.tonalElevation = o.tonalElevation.value_or(SearchBarDefaults::TonalElevation);
        so.shadowElevation = o.shadowElevation.value_or(SearchBarDefaults::ShadowElevation);
        const Dp windowHeight = internal::WindowContainerHeight();
        Surface(Width(ZIndex(modifier, 1), internal::SearchBarMinWidth), so, [=] {
            Column({}, Arrangement::Top(), ui::AlignmentH::Start(), [=] {
                inputField();
                const EnterTransition enter = FadeIn(internal::AnimationEnterFloatSpec()) + ExpandVertically(internal::AnimationEnterFloatSpec());
                const ExitTransition exit = FadeOut(internal::AnimationExitFloatSpec()) + ShrinkVertically(internal::AnimationExitFloatSpec());
                AnimatedVisibility(expanded, {}, [=] {
                    const Dp maxHeight{ windowHeight.value * internal::DockedExpandedTableMaxHeightScreenRatio };
                    const Dp minHeight{ std::min(internal::DockedExpandedTableMinHeight.value, maxHeight.value) };
                    Column(SizeIn({}, Dp::Unspecified(), minHeight, Dp::Unspecified(), maxHeight), Arrangement::Top(), ui::AlignmentH::Start(), [=] {
                        HorizontalDivider({}, DividerDefaults::Thickness, colors.dividerColor);
                        content();
                    });
                }, enter, exit);
            });
        });
        BackHandler(expanded, [onExpandedChange] { if (onExpandedChange) onExpandedChange(false); });
    }

}
