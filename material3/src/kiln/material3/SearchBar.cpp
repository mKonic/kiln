#include "kiln/material3/SearchBar.h"

#include "kiln/animation/Easing.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Back.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draggable.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/Divider.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/TextFieldDefaults.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/SearchBarInternal.h"
#include "kiln/material3/internal/TextFieldDecoration.h"
#include "kiln/material3/tokens/AppBarTokens.h"
#include "kiln/material3/tokens/ElevationTokens.h"
#include "kiln/material3/tokens/FilledTextFieldTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/MotionTokens.h"
#include "kiln/material3/tokens/ScrimTokens.h"
#include "kiln/material3/tokens/SearchBarTokens.h"
#include "kiln/material3/tokens/SearchViewTokens.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Configuration.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace internal {

        anim::Easing ToEasing(const CubicBezierEasing& e) { return anim::Easing::CubicBezier(e.a, e.b, e.c, e.d); }

        anim::SpecPtr AnimationEnterFloatSpec() {
            static const anim::SpecPtr s = anim::Tween(int(MotionTokens::DurationLong4), AnimationDelayMillis,
                                                       ToEasing(MotionTokens::EasingEmphasizedDecelerateCubicBezier));
            return s;
        }
        anim::SpecPtr AnimationExitFloatSpec() {
            static const anim::SpecPtr s = anim::Tween(int(MotionTokens::DurationMedium3), AnimationDelayMillis, anim::Easing::CubicBezier(0, 1, 0, 1));
            return s;
        }
        anim::SpecPtr AnimationPredictiveBackExitFloatSpec() {
            static const anim::SpecPtr s = anim::Tween(int(MotionTokens::DurationMedium3), 0, anim::Easing::CubicBezier(0, 1, 0, 1));
            return s;
        }
        anim::SpecPtr AnimationForContentFadeInSpec() {
            static const anim::SpecPtr s = anim::Tween(int(MotionTokens::DurationShort2), int(MotionTokens::DurationShort1),
                                                       ToEasing(MotionTokens::EasingStandardAccelerateCubicBezier));
            return s;
        }
        anim::SpecPtr AnimationForContentFadeOutSpec() {
            static const anim::SpecPtr s = anim::Tween(int(MotionTokens::DurationShort2), 0, ToEasing(MotionTokens::EasingStandardDecelerateCubicBezier));
            return s;
        }

        // getWindowContainerHeight (the screen height in dp).
        Dp WindowContainerHeight() {
            auto& c = *rt::Composition::Current();
            const int fromConfig = c.Read(ui::LocalConfiguration).screenHeightDp;
            if (fromConfig > 0) return Dp(float(fromConfig));
            if (const ui::Owner* owner = c.Read(ui::LocalOwner)) {
                const float density = owner->GetDensity().density;
                const ui::IntSize size = const_cast<ui::Owner*>(owner)->Root().Size();
                if (size.height > 0 && density > 0) return Dp(float(size.height) / density);
            }
            return Dp(800);
        }

        int Lerp(int a, int b, float f) { return int(std::lround(float(a) + float(b - a) * f)); }
        float PredictiveBackTransform(float progress) {
            static const anim::Easing easing = anim::Easing::CubicBezier(0.1f, 0.1f, 0.0f, 1.0f);   // PredictiveBack.transform
            return easing.Transform(progress);
        }
    }

    // --- SearchBarState -------------------------------------------------------------------------

    SearchBarState::SearchBarState(SearchBarValue initialValue, anim::SpecPtr expand, anim::SpecPtr collapse, anim::SpecPtr fadeIn, anim::SpecPtr fadeOut)
        : SearchBarState(initialValue == SearchBarValue::Expanded ? 1.0f : 0.0f, initialValue == SearchBarValue::Expanded ? 1.0f : 0.0f, std::move(expand),
                         std::move(collapse), std::move(fadeIn), std::move(fadeOut)) {}

    SearchBarState::SearchBarState(float progress, float contentProgress, anim::SpecPtr expand, anim::SpecPtr collapse, anim::SpecPtr fadeIn,
                                   anim::SpecPtr fadeOut)
        : animationSpecForExpand(std::move(expand)), animationSpecForCollapse(std::move(collapse)),
          animationSpecForContentFadeIn(fadeIn ? std::move(fadeIn) : anim::Snap()), animationSpecForContentFadeOut(fadeOut ? std::move(fadeOut) : anim::Snap()),
          m_Animatable(std::make_shared<anim::Animatable>(progress)), m_ContentAnimatable(std::make_shared<anim::Animatable>(contentProgress)) {
        Derive();
    }

    float SearchBarState::Progress() const { return std::clamp(m_Animatable->Float(), 0.0f, 1.0f); }
    float SearchBarState::ContentProgress() const { return std::clamp(m_ContentAnimatable->Float(), 0.0f, 1.0f); }
    SearchBarValue SearchBarState::TargetValue() const { return m_Animatable->Target()[0] == 1.0f ? SearchBarValue::Expanded : SearchBarValue::Collapsed; }

    void SearchBarState::Derive() {
        // currentValue (derivedStateOf): within a spring's tolerance of collapsed counts as collapsed.
        const SearchBarValue v = m_Animatable->Value()[0] <= 0.02f ? SearchBarValue::Collapsed : SearchBarValue::Expanded;
        if (m_Current.Peek() != v) m_Current.Set(v);
    }

    std::optional<int> SearchBarState::CollapsedWidth() const {
        if (!collapsedPlaced.Get()) return std::nullopt;
        return int(std::lround(collapsedBounds.Get().Width()));
    }

    namespace {
        rt::Task<> AnimateFloat(std::shared_ptr<anim::Animatable> a, float target, anim::SpecPtr spec, std::function<void()> onFrame = {}) {
            co_await a->AnimateTo(anim::Vec{ target, 0, 0, 0 }, std::move(spec), std::nullopt, [onFrame](const anim::Vec&) { if (onFrame) onFrame(); });
            if (onFrame) onFrame();
        }
        // coroutineScope { launch { a }; launch { b } }: b runs beside a; both finish (or are
        // cancelled together) before this returns.
        rt::Task<> Both(rt::Task<> first, rt::Task<> second) {
            rt::Job other = rt::Launch(rt::Dispatcher::Main(), std::move(second));
            co_await std::move(first);
            while (!other.Done()) co_await rt::NextFrame{};
        }
    }

    rt::Task<> SearchBarState::AnimateToExpanded() {
        co_await Both(AnimateFloat(m_Animatable, 1, animationSpecForExpand, [this] { Derive(); }),
                      AnimateFloat(m_ContentAnimatable, 1, animationSpecForContentFadeIn));
    }

    rt::Task<> SearchBarState::AnimateToCollapsed() {
        co_await Both(AnimateFloat(m_Animatable, 0, animationSpecForCollapse, [this] { Derive(); }),
                      AnimateFloat(m_ContentAnimatable, 0, animationSpecForContentFadeOut));
    }

    void SearchBarState::SnapTo(float fraction) {
        m_Animatable->SnapTo(fraction);
        Derive();
    }

    namespace {
        std::shared_ptr<SearchBarState> RememberState(SearchBarValue initial, anim::SpecPtr expand, anim::SpecPtr collapse, anim::SpecPtr fadeIn,
                                                      anim::SpecPtr fadeOut) {
            // SearchBarState.Saver keeps both progress values.
            rt::Saver<std::shared_ptr<SearchBarState>> saver{
                [](const std::shared_ptr<SearchBarState>& s) { return rt::SavedValue(rt::SavedValue::List{ double(s->Progress()), double(s->ContentProgress()) }); },
                [=](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<SearchBarState>> {
                    const auto* list = std::get_if<rt::SavedValue::List>(&saved.v);
                    if (!list || list->size() != 2) return std::nullopt;
                    const auto p = rt::SavedNumber((*list)[0]), cp = rt::SavedNumber((*list)[1]);
                    if (!p || !cp) return std::nullopt;
                    return std::make_shared<SearchBarState>(float(*p), float(*cp), expand, collapse, fadeIn, fadeOut);
                }
            };
            return rt::RememberSaveable<std::shared_ptr<SearchBarState>>(
                [&] { return std::make_shared<SearchBarState>(initial, expand, collapse, fadeIn, fadeOut); }, saver,
                rt::Keys(int(initial), reinterpret_cast<std::uintptr_t>(expand.get()), reinterpret_cast<std::uintptr_t>(collapse.get()),
                         reinterpret_cast<std::uintptr_t>(fadeIn.get()), reinterpret_cast<std::uintptr_t>(fadeOut.get())));
        }
    }

    std::shared_ptr<SearchBarState> RememberSearchBarState(SearchBarValue initial, anim::SpecPtr expand, anim::SpecPtr collapse) {
        return RememberState(initial, expand ? expand : Value(MotionSchemeKeyTokens::SlowSpatial), collapse ? collapse : Value(MotionSchemeKeyTokens::DefaultSpatial),
                             nullptr, nullptr);
    }
    std::shared_ptr<SearchBarState> RememberContainedSearchBarState(SearchBarValue initial, anim::SpecPtr expand, anim::SpecPtr collapse, anim::SpecPtr fadeIn,
                                                                    anim::SpecPtr fadeOut) {
        return RememberState(initial, expand ? expand : Value(MotionSchemeKeyTokens::FastSpatial), collapse ? collapse : Value(MotionSchemeKeyTokens::FastSpatial),
                             fadeIn ? fadeIn : internal::AnimationForContentFadeInSpec(), fadeOut ? fadeOut : internal::AnimationForContentFadeOutSpec());
    }
    std::shared_ptr<SearchBarState> RememberSearchBarWithGapState(SearchBarValue initial, anim::SpecPtr expand, anim::SpecPtr collapse, anim::SpecPtr fadeIn,
                                                                  anim::SpecPtr fadeOut) {
        return RememberState(initial, expand ? expand : Value(MotionSchemeKeyTokens::DefaultSpatial),
                             collapse ? collapse : Value(MotionSchemeKeyTokens::FastSpatial),
                             fadeIn ? fadeIn : internal::AnimationForContentFadeInSpec(), fadeOut ? fadeOut : internal::AnimationForContentFadeOutSpec());
    }

    // --- scrolling ------------------------------------------------------------------------------

    SearchBarScrollState::SearchBarScrollState(float limit, float offset, float contentOffset)
        : m_Limit(limit), m_ScrollOffset(offset), m_ContentOffset(contentOffset) {}

    void SearchBarScrollState::SetScrollOffset(float offset) {
        const float v = std::clamp(offset, std::min(m_Limit.Peek(), 0.0f), 0.0f);
        if (m_ScrollOffset.Peek() != v) m_ScrollOffset.Set(v);
    }

    std::shared_ptr<SearchBarScrollState> RememberSearchBarScrollState(float limit, float offset, float contentOffset) {
        rt::Saver<std::shared_ptr<SearchBarScrollState>> saver{
            [](const std::shared_ptr<SearchBarScrollState>& s) {
                return rt::SavedValue(rt::SavedValue::List{ double(s->ScrollOffsetLimit()), double(s->ScrollOffset()), double(s->ContentOffset()) });
            },
            [](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<SearchBarScrollState>> {
                const auto* list = std::get_if<rt::SavedValue::List>(&saved.v);
                if (!list || list->size() != 3) return std::nullopt;
                const auto a = rt::SavedNumber((*list)[0]), b = rt::SavedNumber((*list)[1]), c = rt::SavedNumber((*list)[2]);
                if (!a || !b || !c) return std::nullopt;
                return std::make_shared<SearchBarScrollState>(float(*a), float(*b), float(*c));
            }
        };
        return rt::RememberSaveable<std::shared_ptr<SearchBarScrollState>>([&] { return std::make_shared<SearchBarScrollState>(limit, offset, contentOffset); },
                                                                          saver, rt::Keys());
    }

    float SearchBarScrollBehavior::OverlappedFraction() const {
        const auto& s = *ScrollState();
        const float limit = s.ScrollOffsetLimit();
        if (limit == 0) return 0;
        return 1 - std::clamp(limit - s.ContentOffset(), limit, 0.0f) / limit;
    }

    namespace {
        // settleSearchBar: what is left of a fling carries the bar on, then it snaps to shown or
        // hidden, whichever is nearer. Returns the velocity left.
        rt::Task<float> SettleSearchBar(std::shared_ptr<SearchBarScrollState> state, float velocity, std::shared_ptr<const anim::DecaySpec> fling,
                                        anim::SpecPtr snap) {
            const float fraction = state->ScrollOffsetLimit() != 0 ? state->ScrollOffset() / state->ScrollOffsetLimit() : 0.0f;
            if (fraction < 0.01f || fraction == 1.0f) co_return 0.0f;
            float remaining = velocity;
            if (fling && std::abs(velocity) > 1) {
                anim::Animatable a(0.0f);
                float last = 0;
                auto r = co_await a.AnimateDecay(velocity, fling, [&](float value) {
                    const float delta = value - last;
                    const float initial = state->ScrollOffset();
                    state->SetScrollOffset(initial + delta);
                    const float consumed = std::abs(initial - state->ScrollOffset());
                    last = value;
                    if (std::abs(delta - consumed) > 0.5f) a.Stop();
                });
                remaining = r.reason == anim::EndReason::Finished ? 0.0f : r.endVelocity[0];
            }
            if (state->ScrollOffsetLimit() < state->ScrollOffset() && state->ScrollOffset() < 0) {
                anim::Animatable a(state->ScrollOffset());
                const float target = fraction < 0.5f ? 0.0f : state->ScrollOffsetLimit();
                co_await a.AnimateTo(anim::Vec{ target, 0, 0, 0 }, snap, std::nullopt, [&](const anim::Vec& v) { state->SetScrollOffset(v[0]); });
            }
            co_return remaining;
        }
        rt::Task<> SettleAfterDrag(std::shared_ptr<SearchBarScrollState> state, float velocity, std::shared_ptr<const anim::DecaySpec> fling, anim::SpecPtr snap) {
            co_await SettleSearchBar(std::move(state), velocity, std::move(fling), std::move(snap));
        }

        struct EnterAlwaysSearchBarScrollBehavior final : SearchBarScrollBehavior, ui::NestedScrollConnection {
            std::shared_ptr<SearchBarScrollState> state;
            std::function<bool()> canScroll;
            bool reverseLayout = false;
            anim::SpecPtr snap;
            std::shared_ptr<const anim::DecaySpec> fling;
            std::shared_ptr<internal::CoroutineScope> scope;
            std::weak_ptr<EnterAlwaysSearchBarScrollBehavior> self;

            bool CanScroll() const { return !canScroll || canScroll(); }
            std::shared_ptr<SearchBarScrollState> ScrollState() const override { return state; }
            std::shared_ptr<ui::NestedScrollConnection> NestedScrollConnection() const override { return self.lock(); }

            ui::Modifier SearchBarScrollBehaviorModifier() const override {
                auto st = state;
                auto& drag = RememberDraggableState([st](float delta) { st->SetScrollOffset(st->ScrollOffset() + delta); });
                DraggableOptions o;
                o.enabled = CanScroll();
                o.onDragStopped = [st, f = fling, s = snap, sc = scope](float velocity) { sc->Launch(SettleAfterDrag(st, velocity, f, s)); };
                ui::Modifier m = Draggable({}, drag, Orientation::Vertical, o);
                m = ClipToBounds(std::move(m));
                m = LayoutMod(std::move(m), [st](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
                    ui::Placeable& p = measurable.Measure(c);
                    const int offset = int(std::lround(st->ScrollOffset()));
                    const int height = std::max(0, p.Height() + offset);
                    return s.Layout(p.Width(), height, [&p, offset](ui::PlacementScope& place) { place.Place(p, 0, offset); });
                });
                return OnSizeChanged(std::move(m), [st](ui::IntSize size) { st->SetScrollOffsetLimit(-float(size.height)); });
            }

            ui::Offset OnPreScroll(ui::Offset available, ui::NestedScrollSource) override {
                if (!CanScroll()) return {};
                const float prev = state->ScrollOffset();
                state->SetScrollOffset(prev + available.y);
                // In between shown and hidden, the bar takes the scroll (not in a reversed layout).
                return !reverseLayout && prev != state->ScrollOffset() ? ui::Offset{ 0, available.y } : ui::Offset{};
            }
            ui::Offset OnPostScroll(ui::Offset consumed, ui::Offset available, ui::NestedScrollSource) override {
                if (!CanScroll()) return {};
                if (reverseLayout && available.y > 0) {
                    // A reversed layout takes a pull down to reveal the bar, not a pull up to hide it.
                    state->SetScrollOffset(state->ScrollOffset() + available.y);
                    state->SetContentOffset(state->ContentOffset() + available.y);
                    return { 0, available.y };
                }
                if (!reverseLayout) {
                    state->SetScrollOffset(state->ScrollOffset() + consumed.y);
                    state->SetContentOffset(state->ContentOffset() + consumed.y);
                }
                return {};
            }
            rt::Task<ui::Offset> OnPostFling(ui::Offset, ui::Offset available) override {
                if (!CanScroll()) co_return ui::Offset{};
                const float left = co_await SettleSearchBar(state, available.y, fling, snap);
                co_return ui::Offset{ 0, left };
            }
        };
    }

    // --- colours --------------------------------------------------------------------------------

    namespace {
        Color LerpScrolled(Color from, Color scrolled, float fraction) {
            if (!scrolled.Specified()) return from;
            static const anim::Easing fastOutLinearIn = anim::FastOutLinearIn();
            return ui::Lerp(from, scrolled, fastOutLinearIn.Transform(fraction));
        }
    }
    Color AppBarWithSearchColors::SearchBarContainerColor(float f) const { return LerpScrolled(searchBarColors.containerColor, scrolledSearchBarContainerColor, f); }
    Color AppBarWithSearchColors::AppBarContainerColor(float f) const { return LerpScrolled(appBarContainerColor, scrolledAppBarContainerColor, f); }

    namespace SearchBarDefaults {
        const Dp TonalElevation = ElevationTokens::Level0;
        const Dp ShadowElevation = ElevationTokens::Level0;
        const Dp InputFieldHeight = SearchBarTokens::ContainerHeight;
        const Dp dockedDropdownGapSize{ 2 };
        ShapePtr inputFieldShape() { return Value(SearchBarTokens::ContainerShape); }
        ShapePtr fullScreenShape() { return Value(SearchViewTokens::FullScreenContainerShape); }
        Color collapsedContainedSearchBarColor() { return Value(SearchBarTokens::ContainerColor); }
        Color fullScreenContainedSearchBarColor() { return Value(ColorSchemeKeyTokens::SurfaceContainerLow); }
        ShapePtr dockedShape() { return Value(SearchViewTokens::DockedContainerShape); }
        ShapePtr dockedDropdownShape() { return RoundedCorner(Dp(12)); }
        Color dockedDropdownScrimColor() { return Value(ScrimTokens::ContainerColor).WithAlpha(ScrimTokens::ContainerOpacity); }
        // systemBarsForVisualComponents (Android): systemBars.union(displayCutout).
        WindowInsets windowInsets() {
            return WindowInsets::SystemBars().Union(WindowInsets::DisplayCutout()).Only(WindowInsetsSides::Horizontal() + WindowInsetsSides::Top());
        }
        WindowInsets fullScreenWindowInsets() { return WindowInsets::SafeDrawing(); }

        std::shared_ptr<SearchBarScrollBehavior> enterAlwaysSearchBarScrollBehavior(std::shared_ptr<SearchBarScrollState> scrollState, std::function<bool()> canScroll,
                                                                                   anim::SpecPtr snap, std::shared_ptr<const anim::DecaySpec> fling, bool reverseLayout) {
            auto& c = *rt::Composition::Current();
            if (!scrollState) scrollState = RememberSearchBarScrollState();
            if (!snap) snap = Value(MotionSchemeKeyTokens::DefaultEffects);
            if (!fling) fling = c.Remember<std::shared_ptr<const anim::DecaySpec>>([&] {
                return std::make_shared<anim::SplineDecay>(c.Read(ui::LocalDensity).density);
            });
            const auto scope = internal::RememberCoroutineScope();
            auto behavior = c.Remember<std::shared_ptr<EnterAlwaysSearchBarScrollBehavior>>(
                [] { return std::make_shared<EnterAlwaysSearchBarScrollBehavior>(); },
                rt::Keys(reinterpret_cast<std::uintptr_t>(scrollState.get()), reinterpret_cast<std::uintptr_t>(snap.get()),
                         reinterpret_cast<std::uintptr_t>(fling.get()), reverseLayout));
            behavior->self = behavior;
            behavior->state = scrollState;
            behavior->canScroll = std::move(canScroll);
            behavior->reverseLayout = reverseLayout;
            behavior->snap = snap;
            behavior->fling = fling;
            behavior->scope = scope;
            return behavior;
        }

        SearchBarColors colors(Color containerColor, Color dividerColor, TextFieldColors inputFieldColors) {
            return SearchBarColors{ containerColor, dividerColor, std::move(inputFieldColors) };
        }
        SearchBarColors colors() { return colors(Value(SearchBarTokens::ContainerColor), Value(SearchViewTokens::DividerColor), inputFieldColors()); }

        SearchBarColors containedColors(const SearchBarState& state) {
            const Color container = state.IsExpanded() ? fullScreenContainedSearchBarColor() : collapsedContainedSearchBarColor();
            TextFieldColors field = inputFieldColors();
            field.focusedContainerColor = field.unfocusedContainerColor = field.disabledContainerColor = collapsedContainedSearchBarColor();
            return colors(container, Value(SearchViewTokens::DividerColor), field);
        }

        AppBarWithSearchColors appBarWithSearchColors() {
            AppBarWithSearchColors c;
            c.searchBarColors = colors();
            c.scrolledSearchBarContainerColor = Value(ColorSchemeKeyTokens::SurfaceContainerHighest);
            c.appBarContainerColor = Value(AppBarTokens::ContainerColor);
            c.scrolledAppBarContainerColor = Value(AppBarTokens::OnScrollContainerColor);
            c.appBarNavigationIconColor = Value(AppBarTokens::LeadingIconColor);
            c.appBarActionIconColor = Value(AppBarTokens::TrailingIconColor);
            return c;
        }

        TextFieldColors inputFieldColors() {
            TextFieldColors c = TextFieldDefaults::colors();
            const Color input = Value(SearchBarTokens::InputTextColor);
            const Color disabledInput = Value(FilledTextFieldTokens::DisabledInputColor).WithAlpha(FilledTextFieldTokens::DisabledInputOpacity);
            c.focusedTextColor = c.unfocusedTextColor = input;
            c.disabledTextColor = disabledInput;
            c.cursorColor = Value(FilledTextFieldTokens::CaretColor);
            c.focusedLeadingIconColor = c.unfocusedLeadingIconColor = Value(SearchBarTokens::LeadingIconColor);
            c.disabledLeadingIconColor =
                Value(FilledTextFieldTokens::DisabledLeadingIconColor).WithAlpha(FilledTextFieldTokens::DisabledLeadingIconOpacity);
            c.focusedTrailingIconColor = c.unfocusedTrailingIconColor = Value(SearchBarTokens::TrailingIconColor);
            c.disabledTrailingIconColor =
                Value(FilledTextFieldTokens::DisabledTrailingIconColor).WithAlpha(FilledTextFieldTokens::DisabledTrailingIconOpacity);
            c.focusedPlaceholderColor = c.unfocusedPlaceholderColor = Value(SearchBarTokens::SupportingTextColor);
            c.disabledPlaceholderColor = disabledInput;
            c.focusedPrefixColor = c.unfocusedPrefixColor = Value(FilledTextFieldTokens::InputPrefixColor);
            c.disabledPrefixColor = Value(FilledTextFieldTokens::InputPrefixColor).WithAlpha(FilledTextFieldTokens::DisabledInputOpacity);
            c.focusedSuffixColor = c.unfocusedSuffixColor = Value(FilledTextFieldTokens::InputSuffixColor);
            c.disabledSuffixColor = Value(FilledTextFieldTokens::InputSuffixColor).WithAlpha(FilledTextFieldTokens::DisabledInputOpacity);
            c.focusedContainerColor = c.unfocusedContainerColor = c.disabledContainerColor = Color::Transparent();
            return c;
        }
    }


    // --- input fields ---------------------------------------------------------------------------

    namespace {
        std::string VisualText(TextFieldState& state, const OutputTransformationPtr& output) {
            TextFieldBuffer buffer(state.Value());
            if (output) output->TransformOutput(buffer);
            return buffer.ToString();
        }

        rt::Task<> ExpandBar(std::shared_ptr<SearchBarState> s) { co_await s->AnimateToExpanded(); }
        rt::Task<> ClearFocusLater(ui::Owner* owner) {
            // The collapse animation starts after this delay anyway (b/261632544).
            co_await rt::Delay(internal::AnimationDelayMillis);
            if (owner) owner->ClearFocus();
        }
        rt::Task<> NoTask() { co_return; }

        // What both input fields share: the BasicTextField with the search semantics, the Search
        // action and the filled decoration on a transparent (or given) container.
        struct FieldParams {
            std::shared_ptr<MutableInteractionSource> source;
            TextFieldColors colors;
            bool expanded = false;
            std::function<void(std::string)> onSearch;
            ui::Modifier modifier;   // the caller's, with the bar's own handlers after it
            bool ringFocus = false;
        };

        void SearchInputField(TextFieldState& state, const FieldParams& p, const SearchBarInputFieldOptions& o) {
            auto& c = *rt::Composition::Current();
            const bool focused = p.source->IsFocused();
            const std::string searchSemantics = GetString(Strings::SearchBarSearch);
            const std::string suggestionsAvailable = GetString(Strings::SuggestionsAvailable);
            const text::TextStyle style = o.textStyle.value_or(c.Read(LocalTextStyle()));
            text::TextStyle merged = style;
            merged.span.color = style.span.color.Specified() ? style.span.color : p.colors.TextColor(o.enabled, false, focused);
            const bool expanded = p.expanded;
            ui::Modifier m = SizeIn(p.modifier, internal::SearchBarMinWidth, SearchBarDefaults::InputFieldHeight, internal::SearchBarMaxWidth, Dp::Unspecified());
            m = Semantics(std::move(m), [searchSemantics, suggestionsAvailable, expanded](ui::SemanticsConfiguration& s) {
                s.contentDescription = { searchSemantics };
                if (expanded) s.stateDescription = suggestionsAvailable;
            });
            const ShapePtr shape = o.shape ? o.shape : SearchBarDefaults::inputFieldShape();
            const TextFieldColors colors = p.colors;
            ScrollState* scroll = o.scrollState ? o.scrollState : &RememberScrollState();
            c.Provide(LocalTextSelectionColors, colors.textSelectionColors, [=, &state] {
                BasicTextFieldOptions b;
                b.enabled = o.enabled;
                b.readOnly = o.readOnly;
                b.textStyle = merged;
                b.cursorBrush = gfx::Brush::SolidColor(colors.CursorColor(false));
                b.keyboardOptions = o.keyboardOptions;
                b.keyboardOptions.imeAction = ui::ImeAction::Search;
                TextFieldState* st = &state;
                const auto onSearch = p.onSearch;
                b.onKeyboardAction = [st, onSearch](std::function<void()>) { if (onSearch) onSearch(st->TextUtf8()); };
                b.lineLimits = o.lineLimits;
                b.interactionSource = p.source;
                b.inputTransformation = o.inputTransformation;
                b.outputTransformation = o.outputTransformation;
                b.scrollState = scroll;
                const auto source = p.source;
                const bool enabled = o.enabled, singleLine = o.lineLimits == TextFieldLineLimits::SingleLine();
                const auto output = o.outputTransformation;
                internal::DecorationBoxSlots slots;
                slots.placeholder = o.placeholder;
                // The bar has 16 dp between its icons and its ends; a text field 12 dp.
                if (o.leadingIcon) slots.leadingIcon = [icon = o.leadingIcon] { Box(OffsetMod({}, internal::SearchBarIconOffsetX, Dp(0)), Alignment::TopStart(), false, icon); };
                if (o.trailingIcon)
                    slots.trailingIcon = [icon = o.trailingIcon] { Box(OffsetMod({}, Dp(-internal::SearchBarIconOffsetX.value), Dp(0)), Alignment::TopStart(), false, icon); };
                slots.prefix = o.prefix;
                slots.suffix = o.suffix;
                b.decorator = [=](const std::function<void()>& inner) {
                    internal::CommonDecorationBox(VisualText(*st, output), inner, TextFieldLabelPosition::Inside(), slots, singleLine, enabled, false, source,
                                                  TextFieldDefaults::contentPaddingWithoutLabel(), colors, [=] {
                                                      const Color container = AnimateColorAsState(colors.ContainerColor(enabled, false, source->IsFocused()),
                                                                                                  Value(MotionSchemeKeyTokens::FastEffects));
                                                      Box(internal::TextFieldBackground({}, [container] { return container; }, shape));
                                                  });
                };
                BasicTextField(state, m, std::move(b));
            });
        }
    }

    namespace SearchBarDefaults {
        void InputField(TextFieldState& textFieldState, std::shared_ptr<SearchBarState> bar, std::function<void(std::string)> onSearch, const ui::Modifier& modifier,
                        const SearchBarInputFieldOptions& o) {
            auto& c = *rt::Composition::Current();
            const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
            const bool focused = source->IsFocused();
            ui::Owner* owner = c.Read(ui::LocalOwner);
            const bool touchMode = !owner || owner->GetInputMode() == ui::InputMode::Touch;
            const auto scope = internal::RememberCoroutineScope();
            const bool expanded = bar->IsExpanded();

            ui::Modifier m = OnKeyEvent(modifier, {}, [bar, scope, owner](const ui::KeyEvent& e) {
                // (Compose's NumPadDirectionDown has no Android key code.)
                if (e.keyCode != ui::keys::DpadDown) return false;
                const bool touch = !owner || owner->GetInputMode() == ui::InputMode::Touch;
                // From a keyboard, Down expands the bar; in an expanded bar it moves to the results.
                if (!touch && !bar->IsExpanded()) {
                    scope->Launch(ExpandBar(bar));
                    return true;
                }
                if (bar->IsExpanded()) {
                    if (owner) owner->MoveFocus(ui::FocusDirection::Down);
                    return true;
                }
                return false;
            });
            m = OnFocusChanged(std::move(m), [bar, scope, owner](ui::FocusState f) {
                const bool touch = !owner || owner->GetInputMode() == ui::InputMode::Touch;
                if (ui::IsFocused(f) && touch) scope->Launch(ExpandBar(bar));
            });

            FieldParams p;
            p.source = source;
            p.colors = o.colors.value_or(inputFieldColors());
            p.expanded = expanded;
            p.onSearch = std::move(onSearch);
            p.modifier = std::move(m);
            SearchInputField(textFieldState, p, o);

            // Most touch expansions come from the focus change; after focusing by keyboard, a
            // later tap does not change focus, so a press released on the field expands too.
            c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(source.get())), [source, bar, scope]() -> std::function<void()> {
                const std::uint64_t token = source->Subscribe([bar, scope](const Interaction& i) {
                    if (i.kind == Interaction::Kind::Press && i.phase == Interaction::Phase::End && !bar->IsExpanded()) scope->Launch(ExpandBar(bar));
                });
                return [source, token] { source->Unsubscribe(token); };
            });

            // Typing in the collapsed bar expands it.
            const std::size_t length = textFieldState.Text().size();
            auto& prevLength = c.Remember<std::shared_ptr<std::size_t>>([length] { return std::make_shared<std::size_t>(length); });
            if (length > *prevLength && focused && !expanded) scope->Launch(ExpandBar(bar));
            *prevLength = length;

            const bool shouldClearFocusOnCollapse = !expanded && focused && touchMode;
            c.LaunchedEffect(rt::Keys(expanded), [shouldClearFocusOnCollapse, owner]() -> rt::Task<> {
                if (shouldClearFocusOnCollapse) return ClearFocusLater(owner);
                return NoTask();
            });
        }

        void InputField(TextFieldState& state, std::function<void(std::string)> onSearch, bool expanded, std::function<void(bool)> onExpandedChange,
                        const ui::Modifier& modifier, const SearchBarInputFieldOptions& o) {
            auto& c = *rt::Composition::Current();
            const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
            const bool focused = source->IsFocused();
            ui::Owner* owner = c.Read(ui::LocalOwner);
            auto& focus = c.Remember<std::shared_ptr<ui::FocusRequester>>([] { return std::make_shared<ui::FocusRequester>(); });
            ui::Modifier m = FocusRequesterModifier(modifier, *focus);
            m = OnFocusChanged(std::move(m), [onExpandedChange](ui::FocusState f) {
                if (ui::IsFocused(f) && onExpandedChange) onExpandedChange(true);
            });
            FieldParams p;
            p.source = source;
            p.colors = o.colors.value_or(inputFieldColors());
            p.expanded = expanded;
            p.onSearch = std::move(onSearch);
            p.modifier = std::move(m);
            SearchBarInputFieldOptions single = o;
            single.lineLimits = TextFieldLineLimits::SingleLine();
            SearchInputField(state, p, single);

            const bool shouldClearFocus = !expanded && focused;
            c.LaunchedEffect(rt::Keys(expanded), [shouldClearFocus, owner]() -> rt::Task<> {
                if (shouldClearFocus) return ClearFocusLater(owner);
                return NoTask();
            });
        }
    }

}
