#include "kiln/material3/adaptive/ThreePaneScaffold.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draggable.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/ScreenModifiers.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/runtime/CoroutineScope.h"
#include "kiln/runtime/SnapshotFlow.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace kiln::material3::adaptive {

    using namespace foundation;
    using ui::Dp;
    using ui::IntOffset;
    using ui::IntSize;
    using Role = ThreePaneScaffoldRole;
    using Kind = PaneAdaptedValue::Kind;

    namespace {
        constexpr std::array<Role, 3> kRoles{ Role::Primary, Role::Secondary, Role::Tertiary };

        // Sets `flag` while alive (a try / finally around a coroutine's suspension).
        struct FlagGuard {
            rt::State<bool>* flag;
            ~FlagGuard() { if (flag) flag->Set(false); }
        };
    }

    // --- State ----------------------------------------------------------------------------------

    rt::Task<> ThreePaneScaffoldState::SnapTo(ThreePaneScaffoldValue target) {
        ++m_Mutation;
        m_PredictiveBack.Set(false);
        co_await m_Transition.SnapTo(std::move(target));
    }

    rt::Task<> ThreePaneScaffoldState::SeekTo(float fraction, std::optional<ThreePaneScaffoldValue> target, bool isPredictiveBackInProgress) {
        ++m_Mutation;
        m_PredictiveBack.Set(isPredictiveBackInProgress);
        co_await m_Transition.SeekTo(fraction, std::move(target));
    }

    rt::Task<> ThreePaneScaffoldState::AnimateTo(std::optional<ThreePaneScaffoldValue> target, anim::SpecPtr spec, bool isPredictiveBackInProgress) {
        const std::uint64_t token = ++m_Mutation;
        m_PredictiveBack.Set(isPredictiveBackInProgress);
        // finally { isPredictiveBackInProgress = false } (unless a newer mutation took over)
        struct Reset { ThreePaneScaffoldState* s; std::uint64_t token; ~Reset() { if (s->m_Mutation == token) s->m_PredictiveBack.Set(false); } } reset{ this, token };
        co_await m_Transition.AnimateTo(std::move(target), std::move(spec));
    }

    foundation::Transition<ThreePaneScaffoldValue>& ThreePaneScaffoldState::RememberTransition(rt::Key key) {
        return foundation::UpdateTransition(m_Transition, key);
    }

    // --- Predictive back scale ------------------------------------------------------------------

    IntOffset PredictiveBackScaleState::Convert(IntOffset offset) const {
        if (!m_InProgress || !m_InProgress()) return offset;
        // resetToPivotedTransform around the centre (TransformOrigin.Center), then map.
        const IntSize size = m_ScaffoldSize ? m_ScaffoldSize() : IntSize{};
        const float px = float(size.width) * 0.5f, py = float(size.height) * 0.5f;
        const float s = scaleAnimatable.Value()[0];
        return { int(std::lround(px + (float(offset.x) - px) * s)), int(std::lround(py + (float(offset.y) - py) * s)) };
    }

    namespace {
        // convertStateProgressToPredictiveBackScale: 1 at 0, approaching 95% towards 1.
        float BackScale(float fraction) {
            const float delta = 1 - PredictiveBackScaleState::PredictiveBackMinScale;
            const float shift = delta / 2, curveScale = delta * delta / 2;
            return curveScale / (fraction + shift) + PredictiveBackScaleState::PredictiveBackMinScale;
        }

        rt::Task<> ScaleBack(PredictiveBackScaleState* s) { co_await s->scaleAnimatable.AnimateTo(1.0f); }

        rt::Task<> CollectBackScale(ThreePaneScaffoldState* state, PredictiveBackScaleState* scale) {
            float previous = 0;
            rt::Job back;   // lives with this collector
            rt::SnapshotFlow<float> flow([state] { return state->ProgressFraction(); });
            co_await flow.Collect([state, scale, &previous, &back](const float& value) {
                if (value == previous) return;
                previous = value;
                if (state->IsPredictiveBackInProgress()) { back = rt::Job(); scale->scaleAnimatable.SnapTo(BackScale(value)); }
                else back = rt::Launch(rt::Dispatcher::Main(), ScaleBack(scale));
            });
        }

        // ThreePaneScaffoldState.CollectPredictiveBackScale
        void CollectPredictiveBackScale(ThreePaneScaffoldState& state, PredictiveBackScaleState& scale) {
            auto& c = *rt::Composition::Current();
            ThreePaneScaffoldState* sp = &state;
            PredictiveBackScaleState* bp = &scale;
            c.LaunchedEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(sp)), [sp, bp] { return CollectBackScale(sp, bp); });
        }
    }

    // --- Pane margins ---------------------------------------------------------------------------

    int PaneMargins::GetPaneLeft(ui::PlacementScope& s, int measuredLeft) const {
        if (!specified) return measuredLeft;
        int r = 0;
        for (const auto& i : insets) r = std::max(r, int(std::lround(s.Current(i.left, 0))));
        return std::max({ measuredLeft, fixedLeft, r });
    }
    int PaneMargins::GetPaneTop(ui::PlacementScope& s, int measuredTop) const {
        if (!specified) return measuredTop;
        int r = 0;
        for (const auto& i : insets) r = std::max(r, int(std::lround(s.Current(i.top, 0))));
        return std::max({ measuredTop, fixedTop, r });
    }
    int PaneMargins::GetPaneRight(ui::PlacementScope& s, int measuredRight, int parentRight) const {
        if (!specified) return measuredRight;
        int r = insets.empty() ? parentRight : INT_MAX;
        for (const auto& i : insets) r = std::min(r, int(std::lround(std::min(s.Current(i.right, 3.4e38f), float(INT_MAX)))));
        return std::min({ measuredRight, parentRight - fixedRight, r });
    }
    int PaneMargins::GetPaneBottom(ui::PlacementScope& s, int measuredBottom, int parentBottom) const {
        if (!specified) return measuredBottom;
        int r = insets.empty() ? parentBottom : INT_MAX;
        for (const auto& i : insets) r = std::min(r, int(std::lround(std::min(s.Current(i.bottom, 3.4e38f), float(INT_MAX)))));
        return std::min({ measuredBottom, parentBottom - fixedBottom, r });
    }

    // --- Parent data ----------------------------------------------------------------------------

    namespace {
        PaneScaffoldParentData& DataIn(ui::ParentData& d) {
            if (auto* p = std::any_cast<PaneScaffoldParentData>(&d.extra)) return *p;
            d.extra = PaneScaffoldParentData{};
            return *std::any_cast<PaneScaffoldParentData>(&d.extra);
        }
        // One parent data node per kind (PreferredWidthNode, PreferredHeightNode, PaneMarginsNode,
        // AnimatedPaneNode, MinTouchTargetSizeNode).
        struct PreferredSize { std::optional<Dp> dp; float proportion = NAN;
            bool operator==(const PreferredSize& o) const { return dp == o.dp && (proportion == o.proportion || (std::isnan(proportion) && std::isnan(o.proportion))); } };
        template<int Which>
        struct PreferredNode final : ui::ModifierNode, ui::ParentDataModifier {
            PreferredSize params;
            explicit PreferredNode(PreferredSize p) : params(p) {}
            void OnParamsChanged() { if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure(); }
            ui::ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ui::ParentData& d) const override {
                auto& p = DataIn(d);
                if (Which == 0) { p.preferredWidth = params.dp; p.preferredWidthInProportion = params.proportion; }
                else { p.preferredHeight = params.dp; p.preferredHeightInProportion = params.proportion; }
            }
        };
        using PreferredWidthNode = PreferredNode<0>;
        using PreferredHeightNode = PreferredNode<1>;

        struct MarginsParams { PaneMargins margins; bool operator==(const MarginsParams&) const { return false; } };
        struct PaneMarginsNode final : ui::ModifierNode, ui::ParentDataModifier {
            MarginsParams params;
            explicit PaneMarginsNode(MarginsParams p) : params(std::move(p)) {}
            void OnParamsChanged() { if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure(); }
            ui::ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ui::ParentData& d) const override { DataIn(d).paneMargins = params.margins; }
        };

        struct FlagParams { bool operator==(const FlagParams&) const = default; };
        struct AnimatedPaneNode final : ui::ModifierNode, ui::ParentDataModifier {
            FlagParams params;
            explicit AnimatedPaneNode(FlagParams p) : params(p) {}
            void OnParamsChanged() {}
            ui::ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ui::ParentData& d) const override { DataIn(d).isAnimatedPane = true; }
        };

        struct TouchParams { Dp size; bool operator==(const TouchParams&) const = default; };
        struct MinTouchTargetSizeNode final : ui::ModifierNode, ui::ParentDataModifier {
            TouchParams params;
            explicit MinTouchTargetSizeNode(TouchParams p) : params(p) {}
            void OnParamsChanged() { if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure(); }
            ui::ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ui::ParentData& d) const override { DataIn(d).minTouchTargetSize = params.size; }
        };

        // splitPaneAndContentModifiers: the size, margin and graphics layer modifiers go on the
        // pane (the scaffold reads them; a layer has to wrap the shadow), the rest on the content.
        std::pair<Modifier, Modifier> SplitPaneAndContentModifiers(const Modifier& m) {
            Modifier pane, content;
            for (const auto& e : m.Elements()) {
                const std::type_index t = e->Type();
                const bool toPane = t == typeid(PreferredWidthNode) || t == typeid(PreferredHeightNode) || t == typeid(PaneMarginsNode) ||
                                    IsGraphicsLayerElement(*e);
                (toPane ? pane : content) = (toPane ? pane : content).Then(Modifier(e));
            }
            return { pane, content };
        }
    }

    const PaneScaffoldParentData* PaneScaffoldParentDataOf(const ui::Measurable& m) {
        return std::any_cast<PaneScaffoldParentData>(&m.GetParentData().extra);
    }

    // --- Approach modifiers ---------------------------------------------------------------------

    namespace {
        // lookaheadOffset / convertOffsetToCurrentCoordinates (AnimateModifierUtils.kt).
        IntOffset LookaheadOffset(const ui::PlacementScope& place, const ui::LookaheadScope& scope) {
            const ui::LayoutCoordinates scopeCoords = scope.LookaheadScopeCoordinates(place);
            const ui::Offset o = scope.LocalLookaheadPositionOf(scopeCoords, *place.Coordinates());
            return { int(std::lround(o.x)), int(std::lround(o.y)) };
        }
        IntOffset ToCurrentCoordinates(const ui::PlacementScope& place, IntOffset offset, const ui::LookaheadScope& scope) {
            const ui::LayoutCoordinates scopeCoords = scope.LookaheadScopeCoordinates(place);
            const ui::Offset current = scopeCoords.LocalPositionOf(*place.Coordinates());
            return { offset.x - int(std::lround(current.x)), offset.y - int(std::lround(current.y)) };
        }

        constexpr IntSize kInvalidSize{ -1, -1 };
        constexpr IntOffset kInvalidOffset{ INT_MIN, INT_MIN };
        struct Rect4 {
            int left = 0, top = 0, right = -1, bottom = -1;   // InvalidIntRect
            bool Valid() const { return !(left == 0 && top == 0 && right == -1 && bottom == -1); }
            int Width() const { return right - left; }
            int Height() const { return bottom - top; }
            bool operator==(const Rect4&) const = default;
            static Rect4 Of(IntOffset o, IntSize s) { return { o.x, o.y, o.x + s.width, o.y + s.height }; }
        };

        // BoundsTracker: the bounds from where the pane was (origin) to where it goes (target).
        struct BoundsTracker {
            anim::SpecPtr spec;
            IntOffset originOffset = kInvalidOffset, targetOffset = kInvalidOffset;
            IntSize originSize = kInvalidSize, targetSize = kInvalidSize;
            Rect4 current;
            Rect4 Origin() const { return originOffset == kInvalidOffset || originSize == kInvalidSize ? Rect4{} : Rect4::Of(originOffset, originSize); }
            Rect4 Target() const { return targetOffset == kInvalidOffset || targetSize == kInvalidSize ? Rect4{} : Rect4::Of(targetOffset, targetSize); }
            void UpdateTargetSize(IntSize s) {
                if (targetSize == s) return;
                originSize = current.Valid() ? IntSize{ current.Width(), current.Height() } : s;
                targetSize = s;
            }
            void UpdateTargetOffset(IntOffset o) {
                if (targetOffset == o) return;
                originOffset = current.Valid() ? IntOffset{ current.left, current.top } : o;
                targetOffset = o;
            }
            Rect4 UpdateAndGetCurrentBounds(float fraction) {
                if (fraction == 0) current = Origin();
                else if (fraction == 1) current = Target();
                else {
                    const Rect4 from = Origin(), to = Target();
                    if (!from.Valid() || !to.Valid()) { current = Rect4{}; return current; }
                    const anim::Vec a{ float(from.left), float(from.top), float(from.right), float(from.bottom) };
                    const anim::Vec b{ float(to.left), float(to.top), float(to.right), float(to.bottom) };
                    const std::int64_t d = spec->DurationNanos(a, b, {}, 4);
                    const anim::Vec v = spec->ValueAt(std::int64_t(double(d) * fraction), a, b, {}, 4);
                    current = { int(std::lround(v[0])), int(std::lround(v[1])), int(std::lround(v[2])), int(std::lround(v[3])) };
                }
                return current;
            }
        };

        // Modifier.animateBounds (adaptive's): the pane's bounds follow the scaffold's motion
        // progress from where it was to where lookahead put it.
        struct AnimateBoundsParams {
            std::function<float()> fraction;
            anim::SpecPtr spec;
            std::function<IntOffset(IntOffset)> scaleConversion;
            const ui::LookaheadScope* scope;
            bool enabled;
            bool operator==(const AnimateBoundsParams&) const { return false; }
        };
        struct AnimateBoundsNode final : ui::ModifierNode, ui::ApproachLayoutModifier {
            AnimateBoundsParams params;
            BoundsTracker tracker;
            explicit AnimateBoundsNode(AnimateBoundsParams p) : params(std::move(p)) { tracker.spec = params.spec; }
            void OnParamsChanged() { tracker.spec = params.spec; InvalidateMeasure(); }
            ui::LayoutModifier* AsLayout() override { return this; }
            bool IsMeasurementApproachInProgress(IntSize) override { return params.enabled && params.fraction() != 1; }
            bool IsPlacementApproachInProgress(ui::PlacementScope&, const ui::LayoutCoordinates&) override { return params.enabled && params.fraction() != 1; }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                ui::Placeable& p = m.Measure(c);
                tracker.UpdateTargetSize({ p.Width(), p.Height() });
                return s.Layout(p.Width(), p.Height(), [this, &p](ui::PlacementScope& place) {
                    if (place.Coordinates()) {
                        tracker.UpdateTargetOffset(LookaheadOffset(place, *params.scope));
                        if (!params.enabled) tracker.UpdateAndGetCurrentBounds(1);
                    }
                    place.Place(p, 0, 0);
                });
            }
            ui::MeasureResult ApproachMeasure(ui::ApproachMeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                if (!params.enabled) {
                    ui::Placeable& p = m.Measure(c);
                    return s.Layout(p.Width(), p.Height(), [&p](ui::PlacementScope& place) { place.Place(p, 0, 0); });
                }
                const Rect4 bounds = tracker.UpdateAndGetCurrentBounds(params.fraction());
                ui::Placeable& p = m.Measure(ui::Constraints::Fixed(std::max(0, bounds.Width()), std::max(0, bounds.Height())));
                return s.Layout(p.Width(), p.Height(), [this, &p, bounds](ui::PlacementScope& place) {
                    if (!place.Coordinates()) return;
                    const IntOffset scaled = params.scaleConversion({ bounds.left, bounds.top });
                    const IntOffset at = ToCurrentCoordinates(place, scaled, *params.scope);
                    place.Place(p, at.x, at.y);
                });
            }
        };
        Modifier AnimateBounds(Modifier m, std::function<float()> fraction, anim::SpecPtr spec, std::function<IntOffset(IntOffset)> scale,
                               const ui::LookaheadScope& scope, bool enabled) {
            return m.With<AnimateBoundsNode>(AnimateBoundsParams{ std::move(fraction), std::move(spec), std::move(scale), &scope, enabled });
        }

        // Modifier.animateWithFading: content that moves fades out at its old place and in at
        // its new one (the drag handle). The approach placement sets the layer's alpha
        // (placeWithLayer { alpha }).
        struct FadingParams {
            std::function<float()> fraction;
            const ui::LookaheadScope* scope;
            bool enabled;
            std::shared_ptr<rt::State<float>> alpha;
            bool operator==(const FadingParams&) const { return false; }
        };
        struct AnimateWithFadingNode final : ui::ModifierNode, ui::ApproachLayoutModifier {
            FadingParams params;
            IntOffset originalOffset = kInvalidOffset, targetOffset = kInvalidOffset;
            anim::SpecPtr spec = anim::Tween();
            explicit AnimateWithFadingNode(FadingParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::LayoutModifier* AsLayout() override { return this; }
            void UpdateTargetOffset(IntOffset o) {
                if (targetOffset == o) return;
                originalOffset = targetOffset;
                targetOffset = o;
            }
            bool IsMeasurementApproachInProgress(IntSize) override { return false; }
            bool IsPlacementApproachInProgress(ui::PlacementScope& place, const ui::LayoutCoordinates&) override {
                UpdateTargetOffset(LookaheadOffset(place, *params.scope));
                return params.enabled && originalOffset != kInvalidOffset && originalOffset != targetOffset && params.fraction() != 1;
            }
            ui::MeasureResult ApproachMeasure(ui::ApproachMeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                // TargetBasedAnimation(-1 -> 1) at the progress: fading out below 0, in above.
                const std::int64_t d = spec->DurationNanos({ -1 }, { 1 }, {}, 1);
                const float value = spec->ValueAt(std::int64_t(double(d) * params.fraction()), { -1 }, { 1 }, {}, 1)[0];
                ui::Placeable& p = m.Measure(c);
                const IntOffset back = value > 0 ? IntOffset{} : IntOffset{ originalOffset.x - targetOffset.x, originalOffset.y - targetOffset.y };
                auto alpha = params.alpha;
                return s.Layout(p.Width(), p.Height(), [&p, back, value, alpha](ui::PlacementScope& place) {
                    if (!place.Coordinates()) return;
                    if (alpha->Peek() != std::abs(value)) alpha->Set(std::abs(value));
                    place.Place(p, back.x, back.y);
                });
            }
        };
        Modifier AnimateWithFading(Modifier m, bool enabled, std::function<float()> fraction, const ui::LookaheadScope& scope) {
            auto& c = *rt::Composition::Current();
            auto& alpha = c.Remember<std::shared_ptr<rt::State<float>>>([] { return std::make_shared<rt::State<float>>(1.0f); });
            auto a = alpha;
            m = m.With<AnimateWithFadingNode>(FadingParams{ std::move(fraction), &scope, enabled, a });
            return GraphicsLayer(std::move(m), [a](GraphicsLayerParams& l) { l.alpha = a->Get(); });
        }
    }

    // --- Scopes ---------------------------------------------------------------------------------

    Modifier ThreePaneScaffoldScope::PreferredWidth(Modifier m, Dp width) const { return m.With<PreferredWidthNode>(PreferredSize{ width, NAN }); }
    Modifier ThreePaneScaffoldScope::PreferredWidth(Modifier m, float proportion) const {
        return m.With<PreferredWidthNode>(PreferredSize{ std::nullopt, std::clamp(proportion, 0.0f, 1.0f) });
    }
    Modifier ThreePaneScaffoldScope::PreferredHeight(Modifier m, Dp height) const { return m.With<PreferredHeightNode>(PreferredSize{ height, NAN }); }
    Modifier ThreePaneScaffoldScope::PreferredHeight(Modifier m, float proportion) const {
        return m.With<PreferredHeightNode>(PreferredSize{ std::nullopt, std::clamp(proportion, 0.0f, 1.0f) });
    }

    Modifier ThreePaneScaffoldScope::PaneMarginsMod(Modifier m, PaddingValues fixed, std::vector<ui::RectRulers> insets) const {
        auto& c = *rt::Composition::Current();
        const ui::Density d = c.Read(ui::LocalDensity);
        const ui::LayoutDirection dir = c.Read(ui::LocalLayoutDirection);
        const bool rtl = fixed.rtlAware && dir == ui::LayoutDirection::Rtl;
        PaneMargins margins;
        margins.specified = true;
        margins.fixedLeft = d.RoundToPx(rtl ? fixed.end : fixed.start);
        margins.fixedRight = d.RoundToPx(rtl ? fixed.start : fixed.end);
        margins.fixedTop = d.RoundToPx(fixed.top);
        margins.fixedBottom = d.RoundToPx(fixed.bottom);
        margins.insets = std::move(insets);
        return m.With<PaneMarginsNode>(MarginsParams{ std::move(margins) });
    }

    float ThreePaneScaffoldScope::MotionProgress() const {
        return m_State->CurrentState() == m_State->TargetState() ? 1.0f : m_State->ProgressFraction();
    }

    Modifier ThreePaneScaffoldScope::PaneExpansionDraggable(Modifier m, PaneExpansionState& state, Dp minTouchTargetSize,
                                                            std::shared_ptr<MutableInteractionSource> interactionSource,
                                                            std::function<void(ui::SemanticsConfiguration&)> semanticsProperties) const {
        PaneExpansionState* sp = &state;
        DraggableOptions drag;
        drag.interactionSource = interactionSource;
        drag.onDragStopped = [sp](float velocity) { sp->LaunchSettle(velocity); };
        m = Draggable(std::move(m), state.DraggableState(), Orientation::Horizontal, drag);
        // semanticsAction: the given semantics' click as a click on the handle.
        if (semanticsProperties) {
            ui::SemanticsConfiguration probe;
            semanticsProperties(probe);
            if (probe.onClick) {
                ClickableOptions click;
                click.interactionSource = interactionSource;
                click.indication = nullptr;
                click.useLocalIndication = false;
                auto action = probe.onClick;
                m = Clickable(std::move(m), [action] { action(); }, click);
            }
        }
        m = SystemGestureExclusion(std::move(m));
        const ThreePaneScaffoldScope* self = this;
        m = AnimateWithFading(std::move(m), true, [self] { return self->MotionProgress(); }, *this);
        if (semanticsProperties) {
            m = Semantics(std::move(m), semanticsProperties, true);
        } else {
            m = Semantics(std::move(m), [sp, locales = CurrentLocales()](ui::SemanticsConfiguration& s) {
                s.contentDescription = { GetString(Strings::AdaptiveDefaultPaneExpansionDragHandleContentDescription, locales) };
                if (const auto current = sp->CurrentAnchor())
                    s.stateDescription = GetString(Strings::AdaptiveDefaultPaneExpansionDragHandleStateDescription, locales, { current->Description(locales) });
                const auto next = sp->NextAnchor();
                if (!next) return;
                s.onClickLabel = GetString(Strings::AdaptiveDefaultPaneExpansionDragHandleActionDescription, locales, { next->Description(locales) });
                const PaneExpansionAnchor target = *next;
                s.onClick = [sp, target] { sp->LaunchAnimateTo(target); return true; };
            }, true);
        }
        return m.With<MinTouchTargetSizeNode>(TouchParams{ minTouchTargetSize });
    }

    // --- AnimatedPane ---------------------------------------------------------------------------

    namespace {
        // focusableInWholeTree: the pane is a focus group; when it takes no input nothing in it
        // can be focused or reached by accessibility.
        Modifier FocusableInWholeTree(Modifier m, bool focusable, Role role) {
            m = FocusGroup(std::move(m));
            m = FocusPropertiesMod(std::move(m), [focusable](ui::FocusProperties& p) {
                if (!focusable) {
                    p.canFocus = false;
                    p.onEnter = [](ui::FocusEnterExitScope& s) { s.CancelFocusChange(); };
                }
            });
            if (focusable) {
                const Strings title = role == Role::Primary ? Strings::AdaptiveDefaultPaneTitlePrimary
                                    : role == Role::Secondary ? Strings::AdaptiveDefaultPaneTitleSecondary : Strings::AdaptiveDefaultPaneTitleTertiary;
                return Semantics(std::move(m), [paneTitle = GetString(title)](ui::SemanticsConfiguration& s) {
                    s.isTraversalGroup = true;
                    s.paneTitle = paneTitle;
                });
            }
            return ClearAndSetSemantics(std::move(m), [](ui::SemanticsConfiguration&) {});
        }

        Modifier LevitatedProperties(Modifier m, const PaneAdaptedValue& value, const ShapePtr& shape, bool hasDragHandle) {
            if (value.kind != Kind::Levitated) return m;
            m = ShadowMod(std::move(m), AnimatedPaneDefaults::ShadowElevation, shape);
            if (value.dragToResizeState && !hasDragHandle) m = DragToResize(std::move(m), value.dragToResizeState, false);
            return m;
        }

        const char* RoleName(Role r) { return r == Role::Primary ? "Primary" : r == Role::Secondary ? "Secondary" : "Tertiary"; }
    }

    void AnimatedPane(const ThreePaneScaffoldPaneScope& scope, const Modifier& modifier, const AnimatedPaneOptions& options, std::function<void()> content) {
        auto& c = *rt::Composition::Current();
        const Role role = scope.paneRole;
        ThreePaneScaffoldMotionDataProvider& motion = scope.MotionDataProvider();
        const EnterTransition enter = options.enterTransition ? *options.enterTransition : CalculateDefaultEnterTransition(motion, role);
        const ExitTransition exit = options.exitTransition ? *options.exitTransition : CalculateDefaultExitTransition(motion, role);
        const anim::SpecPtr boundsSpec = options.boundsAnimationSpec ? options.boundsAnimationSpec : PaneMotionDefaults::AnimationSpec();
        const ShapePtr shape = options.shape ? options.shape : RectangleShape();
        PredictiveBackScaleState* backScale = &motion.predictiveBackScaleState();
        auto scaleConversion = [backScale](IntOffset o) { return backScale->Convert(o); };
        const bool animatingBounds = scope.paneMotion == PaneMotion::AnimateBounds;
        auto& transition = scope.ScaffoldStateTransition();
        const PaneAdaptedValue paneValue = transition.TargetState()[role];
        auto [paneModifier, contentModifier] = SplitPaneAndContentModifiers(modifier);
        const ThreePaneScaffoldScope* scaffold = &scope;

        Modifier m = Modifier{}.With<AnimatedPaneNode>(FlagParams{});
        m = AnimateBounds(std::move(m), [scaffold] { return scaffold->MotionProgress(); }, boundsSpec, scaleConversion, scope, animatingBounds);
        m = FocusRequesterModifier(std::move(m), scope.FocusRequesterOf(role));
        m = FocusableInWholeTree(std::move(m), scope.isInteractable, role);
        m = LevitatedProperties(std::move(m), paneValue, shape, options.dragToResizeHandle != nullptr);
        if (!animatingBounds) m = ClipToBounds(std::move(m));
        m = m.Then(paneModifier);
        m = Clip(std::move(m), shape);

        rt::SaveableStateHolder* holder = &scope.SaveableStateHolder();
        auto handle = options.dragToResizeHandle;
        foundation::AnimatedVisibility<ThreePaneScaffoldValue>(
            transition, [role](const ThreePaneScaffoldValue& v) { return v[role].kind != Kind::Hidden; }, m,
            [holder, role, contentModifier, paneValue, handle, content] {
                rt::SaveableStateProvider(*holder, RoleName(role), [contentModifier, paneValue, handle, content] {
                    Column(contentModifier, [paneValue, handle, content] {
                        if (paneValue.kind == Kind::Levitated && paneValue.dragToResizeState && handle) {
                            auto state = paneValue.dragToResizeState;
                            Box(DragToResize(WrapContent(FillMax(Modifier{}, Axis::Horizontal), Axis::Vertical), state, true), ui::Alignment::Center(), false,
                                [handle, state] { handle(*state); });
                        }
                        if (content) content();
                    });
                });
            },
            enter, exit, rt::Key("pane"));

        // The scrim of a levitated pane, shown and hidden with it.
        auto& scrim = c.Remember<ScrimContent>([] { return ScrimContent{}; });
        if (paneValue.kind == Kind::Levitated) scrim = paneValue.scrim;
        if (scrim) {
            auto s = scrim;
            foundation::AnimatedVisibility<ThreePaneScaffoldValue>(
                transition, [role](const ThreePaneScaffoldValue& v) { return v[role].kind != Kind::Hidden; }, Modifier{},
                [s] { if (*s) (*s)(); }, enter, exit, rt::Key("scrim"));
        }
    }

    void LevitatedPaneScrim(const Modifier& modifier, std::function<void()> onClick, std::optional<ui::Color> color) {
        Modifier m = Background(FillMax(modifier, Axis::Both), color.value_or(LevitatedPaneScrimDefaults::Color));
        // With the default (no-op) click, the scrim is hidden from accessibility.
        if (!onClick) m = ClearAndSetSemantics(std::move(m), [](ui::SemanticsConfiguration&) {});
        ClickableOptions click;
        click.indication = nullptr;
        click.useLocalIndication = false;
        m = Clickable(std::move(m), [onClick] { if (onClick) onClick(); }, click);
        Box(m);
    }

    // --- The layout -----------------------------------------------------------------------------

    namespace {
        struct Bounds {
            int left = 0, top = 0, right = 0, bottom = 0;
            int Width() const { return right - left; }
            int Height() const { return bottom - top; }
        };

        struct PaneMeasurable {
            ui::Measurable* measurable;
            int priority;
            Role role;
            PaneAdaptedValue value;
            int measuringWidth, measuringHeight;
            PaneMargins margins;
            bool isAnimatedPane;
            float zIndex;
            std::optional<Bounds> measuredBounds;

            PaneMeasurable(ui::Measurable* m, int priority, Role role, PaneAdaptedValue value, int defaultWidth, int defaultHeight,
                           const ui::Density& density, IntSize scaffoldSize)
                : measurable(m), priority(priority), role(role), value(std::move(value)) {
                const PaneScaffoldParentData* d = PaneScaffoldParentDataOf(*m);
                const PaneScaffoldParentData data = d ? *d : PaneScaffoldParentData{};
                measuringWidth = data.preferredWidth ? density.RoundToPx(*data.preferredWidth)
                               : std::isfinite(data.preferredWidthInProportion) ? int(float(scaffoldSize.width) * data.preferredWidthInProportion) : defaultWidth;
                measuringHeight = data.preferredHeight ? density.RoundToPx(*data.preferredHeight)
                                : std::isfinite(data.preferredHeightInProportion) ? int(float(scaffoldSize.height) * data.preferredHeightInProportion) : defaultHeight;
                margins = data.paneMargins;
                isAnimatedPane = data.isAnimatedPane;
                zIndex = this->value.kind == Kind::Levitated ? ThreePaneScaffoldDefaults::LevitatedPaneZIndex : 0;
            }
            int MeasuredWidth() const { return measuredBounds ? std::max(measuredBounds->Width(), 0) : 0; }
            int MeasuredHeight() const { return measuredBounds ? std::max(measuredBounds->Height(), 0) : 0; }
            int PlacedX() const { return measuredBounds ? measuredBounds->left : 0; }
            int PlacedY() const { return measuredBounds ? measuredBounds->top : 0; }
            std::shared_ptr<DragToResizeState> DragState() const { return value.kind == Kind::Levitated ? value.dragToResizeState : nullptr; }

            void DoMeasureAndPlace(ui::PlacementScope& place, const Bounds& scaffold) {
                if (measuredBounds) {
                    Bounds b = *measuredBounds;
                    measuredBounds = Bounds{ margins.GetPaneLeft(place, b.left), margins.GetPaneTop(place, b.top),
                                             margins.GetPaneRight(place, b.right, scaffold.Width()), margins.GetPaneBottom(place, b.bottom, scaffold.Height()) };
                }
                ui::Placeable& p = measurable->Measure(ui::Constraints::Fixed(MeasuredWidth(), MeasuredHeight()));
                place.Place(p, PlacedX(), PlacedY(), zIndex);
            }
        };

        struct DragHandleMeasurable {
            ui::Measurable* measurable;
            int minTouchTargetSize;
            bool isVisible = false;
            int minWidth = 0, maxHeight = INT_MAX;
            std::optional<IntOffset> center;
            void DoMeasureAndPlace(ui::PlacementScope& place) {
                if (!isVisible) return;
                ui::Placeable& p = measurable->Measure(ui::Constraints{ minWidth, ui::Constraints::Infinity, 0, maxHeight });
                const IntOffset c = center.value_or(IntOffset{});
                place.Place(p, c.x - p.Width() / 2, c.y - p.Height() / 2);
            }
        };

        // ThreePaneContentMeasurePolicy.
        class ThreePanePolicy final : public MultiContentMeasurePolicy {
        public:
            ThreePanePolicy(PaneExpansionState* expansion, ThreePaneScaffoldMotionDataProvider* motion) : m_Expansion(expansion), m_Motion(motion) {}
            rt::State<PaneScaffoldDirective> directive{ PaneScaffoldDirective{} };
            rt::State<ThreePaneScaffoldValue> value{ ThreePaneScaffoldValue{} };
            rt::State<ThreePaneScaffoldHorizontalOrder> order{ ThreePaneScaffoldHorizontalOrder{ Role::Primary, Role::Secondary, Role::Tertiary } };

            ui::MeasureResult Measure(ui::MeasureScope& s, const std::vector<std::vector<ui::Measurable*>>& contents, const ui::Constraints& c) override {
                auto first = [&](std::size_t i) -> ui::Measurable* { return i < contents.size() && !contents[i].empty() ? contents[i][0] : nullptr; };
                ui::Measurable* panes[3] = { first(0), first(1), first(2) };
                ui::Measurable* dragHandle = first(3);
                ui::Measurable* scrim = nullptr;   // getScrimMeasurable: an AnimatedPane's own scrim, else the default one
                for (std::size_t i = 0; i < 3 && !scrim; ++i) if (i < contents.size() && contents[i].size() > 1) scrim = contents[i][1];
                if (!scrim) scrim = first(4);
                const bool lookingAhead = s.IsLookingAhead();
                const ui::Density density = s;
                const ui::LayoutDirection dir = s.Direction();
                const PaneScaffoldDirective dirv = directive.Get();
                const ThreePaneScaffoldValue val = value.Get();
                const ThreePaneScaffoldHorizontalOrder ord = order.Get();
                // Reading the expansion here subscribes the measure to it.
                (void)m_Expansion->IsUnspecified();
                return s.Layout(c.maxWidth, c.maxHeight, [=, this](ui::PlacementScope& place) {
                    Place(place, panes, dragHandle, scrim, c, lookingAhead, density, dir, dirv, val, ord);
                });
            }

        private:
            std::vector<PaneMeasurable> PanesWith(ui::Measurable* const panes[3], const ThreePaneScaffoldValue& val, const ThreePaneScaffoldHorizontalOrder& ord,
                                                  const PaneScaffoldDirective& dirv, const ui::Density& density, IntSize size,
                                                  const std::function<bool(const PaneAdaptedValue&)>& predicate) {
                std::vector<PaneMeasurable> out;
                for (int i = 0; i < 3; ++i) {
                    const Role role = ord[i];
                    const PaneAdaptedValue& v = val[role];
                    if (!predicate(v) || !panes[int(role)]) continue;
                    const int priority = role == Role::Primary ? ThreePaneScaffoldDefaults::PrimaryPanePriority
                                       : role == Role::Secondary ? ThreePaneScaffoldDefaults::SecondaryPanePriority : ThreePaneScaffoldDefaults::TertiaryPanePriority;
                    out.emplace_back(panes[int(role)], priority, role, v, density.RoundToPx(dirv.defaultPanePreferredWidth),
                                     density.RoundToPx(dirv.defaultPanePreferredHeight), density, size);
                }
                return out;
            }

            void RecordMeasureResult(PaneMeasurable& p, bool lookingAhead) {
                PaneMotionData& d = (*m_Motion)[p.role];
                if (!lookingAhead && p.value.kind != Kind::Hidden) {
                    // Visible panes keep their zIndex for when they are hidden.
                    d.zIndex = p.zIndex;
                    return;
                }
                if (!p.measuredBounds) return;
                const Bounds& b = *p.measuredBounds;
                if (!d.isOriginSizeAndPositionSet) {
                    // Only the first measurement of a motion is its origin.
                    d.originSize = d.isTargetSizeAndPositionSet ? d.targetSize : IntSize{ b.Width(), b.Height() };
                    d.originPosition = d.isTargetSizeAndPositionSet ? d.targetPosition : IntOffset{ b.left, b.top };
                    d.isOriginSizeAndPositionSet = true;
                }
                d.targetSize = { b.Width(), b.Height() };
                d.targetPosition = { b.left, b.top };
                d.isTargetSizeAndPositionSet = true;
            }

            void MeasureAndPlacePane(const Bounds& b, PaneMeasurable& p, bool lookingAhead) {
                p.measuredBounds = b;
                RecordMeasureResult(p, lookingAhead);
            }

            void MeasureAndPlacePanesInPartition(Bounds partition, int horizontalSpacer, PaneMeasurable& expanded,
                                                 std::vector<PaneMeasurable>& reflowed, bool lookingAhead) {
                PaneMeasurable* reflowedPane = reflowed.empty() ? nullptr : &reflowed[0];
                if (reflowedPane && reflowedPane->value.kind == Kind::Reflowed && reflowedPane->value.reflowUnder == expanded.role) {
                    const int available = partition.Height() - horizontalSpacer;
                    expanded.measuringHeight = std::max(available - reflowedPane->measuringHeight, available / 2);
                    reflowedPane->measuringHeight = available - expanded.measuringHeight;
                } else {
                    expanded.measuringHeight = partition.Height();
                }
                if (reflowedPane) {
                    Bounds b = partition;
                    b.top = partition.top + expanded.measuringHeight + horizontalSpacer;
                    MeasureAndPlacePane(b, *reflowedPane, lookingAhead);
                }
                Bounds b = partition;
                b.bottom = partition.top + expanded.measuringHeight;
                MeasureAndPlacePane(b, expanded, lookingAhead);
            }

            void MeasureAndPlacePartitionsInBounds(Bounds bounds, int verticalSpacer, int horizontalSpacer, std::vector<PaneMeasurable*> expanded,
                                                   std::vector<PaneMeasurable>& reflowed, bool lookingAhead) {
                if (expanded.empty()) return;
                const int allocatable = bounds.Width() - int(expanded.size() - 1) * verticalSpacer;
                int totalPreferred = 0;
                for (auto* p : expanded) totalPreferred += p->measuringWidth;
                if (allocatable > totalPreferred) {
                    // The rest goes to the pane with the highest priority.
                    auto* top = *std::max_element(expanded.begin(), expanded.end(), [](auto* a, auto* b) { return a->priority < b->priority; });
                    top->measuringWidth += allocatable - totalPreferred;
                } else if (allocatable < totalPreferred) {
                    const float scale = float(allocatable) / float(totalPreferred);
                    for (auto* p : expanded) p->measuringWidth = int(float(p->measuringWidth) * scale);
                }
                int x = bounds.left;
                for (auto* p : expanded) {
                    MeasureAndPlacePanesInPartition({ x, bounds.top, x + p->measuringWidth, bounds.bottom }, horizontalSpacer, *p, reflowed, lookingAhead);
                    x += p->measuringWidth + verticalSpacer;
                }
            }

            void PlaceLevitatedPanes(std::vector<PaneMeasurable>& panes, const Bounds& scaffold, ui::LayoutDirection dir, const ui::Density& density,
                                     bool lookingAhead) {
                for (auto& p : panes) {
                    const int measuringWidth = std::min(p.measuringWidth, scaffold.Width());
                    const int measuringHeight = std::min(p.measuringHeight, scaffold.Height());
                    const auto drag = p.DragState();
                    const IntSize size{ drag ? drag->GetDraggedWidth(measuringWidth, density.RoundToPx(ThreePaneScaffoldDefaults::MinPaneWidth), scaffold.Width())
                                             : measuringWidth,
                                        drag ? drag->GetDraggedHeight(measuringHeight, density.RoundToPx(ThreePaneScaffoldDefaults::MinPaneHeight), scaffold.Height())
                                             : measuringHeight };
                    const ui::Alignment alignment = p.value.kind == Kind::Levitated ? p.value.alignment : ui::Alignment::Center();
                    const IntOffset o = alignment.Align(size, { scaffold.Width(), scaffold.Height() }, dir);
                    MeasureAndPlacePane({ o.x, o.y, o.x + size.width, o.y + size.height }, p, lookingAhead);
                }
            }

            void PlaceHiddenPanes(std::vector<PaneMeasurable>& panes) {
                // Hidden panes keep the bounds they had while they animate away.
                for (auto& p : panes) {
                    if (!p.isAnimatedPane) return;   // not animated: nothing to keep
                    const PaneMotionData& d = (*m_Motion)[p.role];
                    p.measuredBounds = Bounds{ d.TargetLeft(), d.TargetTop(), d.TargetRight(), d.TargetBottom() };
                    p.zIndex = d.zIndex + ThreePaneScaffoldDefaults::HiddenPaneZIndexOffset;
                }
            }

            static int SpacerMiddleOffsetX(const PaneMeasurable& left, const PaneMeasurable& right) {
                if (left.measuredBounds && right.measuredBounds) return (left.PlacedX() + left.MeasuredWidth() + right.PlacedX()) / 2;
                if (left.measuredBounds) return left.PlacedX() + left.MeasuredWidth();
                if (right.measuredBounds) return 0;
                return PaneExpansionState::Unspecified;
            }

            void Place(ui::PlacementScope& place, ui::Measurable* const panes[3], ui::Measurable* dragHandleMeasurable, ui::Measurable* scrim,
                       const ui::Constraints& c, bool lookingAhead, const ui::Density& density, ui::LayoutDirection dir,
                       const PaneScaffoldDirective& dirv, const ThreePaneScaffoldValue& val, const ThreePaneScaffoldHorizontalOrder& ord) {
                if (!place.Coordinates()) return;
                const IntSize scaffoldSize{ c.maxWidth, c.maxHeight };
                m_Motion->scaffoldSize = scaffoldSize;
                const Bounds outer{ 0, 0, c.maxWidth, c.maxHeight };
                auto is = [](Kind k) { return [k](const PaneAdaptedValue& v) { return v.kind == k; }; };
                std::vector<PaneMeasurable> expandedPanes = PanesWith(panes, val, ord, dirv, density, scaffoldSize, is(Kind::Expanded));
                std::vector<PaneMeasurable> reflowedPanes = PanesWith(panes, val, ord, dirv, density, scaffoldSize, is(Kind::Reflowed));
                std::vector<PaneMeasurable> levitatedPanes = PanesWith(panes, val, ord, dirv, density, scaffoldSize, is(Kind::Levitated));
                std::vector<PaneMeasurable> hiddenPanes = PanesWith(panes, val, ord, dirv, density, scaffoldSize, is(Kind::Hidden));
                std::optional<DragHandleMeasurable> dragHandle;
                if (dragHandleMeasurable) {
                    const PaneScaffoldParentData* d = PaneScaffoldParentDataOf(*dragHandleMeasurable);
                    const Dp min = d && d->minTouchTargetSize ? *d->minTouchTargetSize : Dp(0);
                    dragHandle = DragHandleMeasurable{ dragHandleMeasurable, density.RoundToPx(min) };
                }
                const int verticalSpacer = density.RoundToPx(dirv.horizontalPartitionSpacerSize);
                const int horizontalSpacer = density.RoundToPx(dirv.verticalPartitionSpacerSize);
                PaneExpansionState& ex = *m_Expansion;
                if (!lookingAhead) ex.OnMeasured(outer.Width(), density, dir);
                std::vector<PaneMeasurable*> expanded;
                for (auto& p : expandedPanes) expanded.push_back(&p);

                if (!ex.IsUnspecified() && expanded.size() == 2) {
                    // The pane expansion overrides everything.
                    const int dragging = ex.CurrentDraggingOffset();
                    if (dragging != PaneExpansionState::Unspecified) {
                        const int half = verticalSpacer / 2;
                        if (dragging <= half) {
                            Bounds b = outer;
                            if (ex.IsDraggingOrSettling()) b.left = dragging * 2 + outer.left;
                            MeasureAndPlacePanesInPartition(b, horizontalSpacer, *expanded[1], reflowedPanes, lookingAhead);
                        } else if (dragging >= outer.Width() - half) {
                            Bounds b = outer;
                            if (ex.IsDraggingOrSettling()) b.right = dragging * 2 - outer.right;
                            MeasureAndPlacePanesInPartition(b, horizontalSpacer, *expanded[0], reflowedPanes, lookingAhead);
                        } else {
                            Bounds l = outer, r = outer;
                            l.right = dragging - half;
                            r.left = dragging + half;
                            MeasureAndPlacePanesInPartition(l, horizontalSpacer, *expanded[0], reflowedPanes, lookingAhead);
                            MeasureAndPlacePanesInPartition(r, horizontalSpacer, *expanded[1], reflowedPanes, lookingAhead);
                        }
                    } else {
                        const int available = c.maxWidth;
                        const int width = ex.FirstPaneWidth();
                        const float proportion = ex.FirstPaneProportion();
                        if (width == 0 || proportion == 0) {
                            MeasureAndPlacePanesInPartition(outer, horizontalSpacer, *expanded[1], reflowedPanes, lookingAhead);
                        } else if (width >= available - verticalSpacer || proportion >= 1) {
                            MeasureAndPlacePanesInPartition(outer, horizontalSpacer, *expanded[0], reflowedPanes, lookingAhead);
                        } else {
                            const int firstWidth = width != PaneExpansionState::Unspecified ? width : int(proportion * float(available - verticalSpacer));
                            const int firstRight = outer.left + firstWidth;
                            Bounds l = outer, r = outer;
                            l.right = firstRight;
                            r.left = firstRight + verticalSpacer;
                            MeasureAndPlacePanesInPartition(l, horizontalSpacer, *expanded[0], reflowedPanes, lookingAhead);
                            MeasureAndPlacePanesInPartition(r, horizontalSpacer, *expanded[1], reflowedPanes, lookingAhead);
                        }
                    }
                } else if (!dirv.excludedBounds.empty()) {
                    // Partitions between the hinges (window bounds, sorted left to right).
                    std::vector<Bounds> partitions;
                    const ui::Offset origin = place.Coordinates()->PositionInRoot();
                    int left = outer.left, right = outer.right;
                    for (const ui::Rect& r : dirv.excludedBounds) {
                        const Bounds e{ int(std::lround(r.left - origin.x)), int(std::lround(r.top - origin.y)), int(std::lround(r.right - origin.x)),
                                        int(std::lround(r.bottom - origin.y)) };
                        if (e.left <= left) left = std::max(left, e.right);
                        else if (e.right >= right) { right = std::min(e.left, right); break; }
                        else {
                            partitions.push_back({ left, outer.top, e.left, outer.bottom });
                            left = std::max(e.right, e.left + verticalSpacer);
                        }
                    }
                    if (left < right) partitions.push_back({ left, outer.top, right, outer.bottom });
                    if (partitions.size() == 1) {
                        MeasureAndPlacePartitionsInBounds(partitions[0], verticalSpacer, horizontalSpacer, expanded, reflowedPanes, lookingAhead);
                    } else if (partitions.size() > 1 && partitions.size() < expanded.size()) {
                        // Two partitions, three panes: two share the wider one.
                        if (partitions[0].Width() > partitions[1].Width()) {
                            MeasureAndPlacePartitionsInBounds(partitions[0], verticalSpacer, horizontalSpacer, { expanded[0], expanded[1] }, reflowedPanes, lookingAhead);
                            MeasureAndPlacePanesInPartition(partitions[1], horizontalSpacer, *expanded[2], reflowedPanes, lookingAhead);
                        } else {
                            MeasureAndPlacePanesInPartition(partitions[0], horizontalSpacer, *expanded[0], reflowedPanes, lookingAhead);
                            MeasureAndPlacePartitionsInBounds(partitions[1], verticalSpacer, horizontalSpacer, { expanded[1], expanded[2] }, reflowedPanes, lookingAhead);
                        }
                    } else if (partitions.size() > 1) {
                        for (std::size_t i = 0; i < expanded.size(); ++i)
                            MeasureAndPlacePanesInPartition(partitions[i], horizontalSpacer, *expanded[i], reflowedPanes, lookingAhead);
                    }
                } else {
                    MeasureAndPlacePartitionsInBounds(outer, verticalSpacer, horizontalSpacer, expanded, reflowedPanes, lookingAhead);
                }

                if (expanded.size() == 2 && dragHandle) {
                    int handleX;
                    if (!ex.IsDraggingOrSettling() || ex.CurrentDraggingOffset() == PaneExpansionState::Unspecified) {
                        handleX = SpacerMiddleOffsetX(*expanded[0], *expanded[1]);
                        if (!lookingAhead) ex.OnExpansionOffsetMeasured(handleX);
                    } else {
                        handleX = ex.CurrentDraggingOffset();
                    }
                    // measureAndPlaceDragHandleIfNeeded
                    if (handleX != PaneExpansionState::Unspecified) {
                        const int minMargin = verticalSpacer / 2;
                        const int clamped = std::clamp(handleX, outer.left + minMargin, std::max(outer.left + minMargin, outer.right - minMargin));
                        const int margin = std::min(clamped - outer.left, outer.right - clamped);
                        const int minWidth = margin < dragHandle->minTouchTargetSize / 2 ? 2 * (dragHandle->minTouchTargetSize - margin) : dragHandle->minTouchTargetSize;
                        dragHandle->isVisible = true;
                        dragHandle->minWidth = minWidth;
                        dragHandle->maxHeight = outer.Height();
                        dragHandle->center = IntOffset{ clamped, (outer.top + outer.bottom) / 2 };
                    }
                } else if (!lookingAhead) {
                    ex.OnExpansionOffsetMeasured(PaneExpansionState::Unspecified);
                }
                PlaceLevitatedPanes(levitatedPanes, outer, dir, density, lookingAhead);
                // Hidden panes stay where they were, so they can animate away.
                PlaceHiddenPanes(hiddenPanes);
                for (auto& p : expandedPanes) p.DoMeasureAndPlace(place, outer);
                for (auto& p : reflowedPanes) p.DoMeasureAndPlace(place, outer);
                if (dragHandle) dragHandle->DoMeasureAndPlace(place);
                if (scrim) place.Place(scrim->Measure(ui::Constraints::Fixed(outer.Width(), outer.Height())), 0, 0);
                for (auto& p : levitatedPanes) p.DoMeasureAndPlace(place, outer);
                for (auto& p : hiddenPanes) p.DoMeasureAndPlace(place, outer);
            }

            PaneExpansionState* m_Expansion;
            ThreePaneScaffoldMotionDataProvider* m_Motion;
        };

        // A hash of what ThreePaneScaffoldValue.equals compares (an effect key).
        std::size_t ValueHash(const ThreePaneScaffoldValue& v) {
            std::size_t h = 0;
            v.ForEach([&](Role, const PaneAdaptedValue& p) {
                h = h * 31 + std::size_t(p.kind);
                h = h * 31 + std::size_t(p.reflowUnder);
                h = h * 31 + std::hash<const void*>{}(p.scrim.get()) + std::hash<const void*>{}(p.dragToResizeState.get());
            });
            return h;
        }

        // calculateThreePaneMotion: updated only while the state changes.
        ThreePaneMotion CalculateThreePaneMotion(ThreePaneScaffoldState& state, const ThreePaneScaffoldHorizontalOrder& ltrOrder) {
            auto& c = *rt::Composition::Current();
            auto& result = c.Remember<ThreePaneMotion>([] { return ThreePaneMotion{}; });
            const ThreePaneScaffoldValue current = state.CurrentState(), target = state.TargetState();
            if (!(current == target)) {
                const auto motions = CalculatePaneMotion(current, target, ltrOrder);
                result = { motions[std::size_t(ltrOrder.IndexOf(Role::Primary))], motions[std::size_t(ltrOrder.IndexOf(Role::Secondary))],
                           motions[std::size_t(ltrOrder.IndexOf(Role::Tertiary))] };
            }
            return result;
        }
    }

    void ThreePaneScaffold(const Modifier& modifier, const PaneScaffoldDirective& directive, ThreePaneScaffoldState& state,
                           const ThreePaneScaffoldHorizontalOrder& paneOrder, PaneContent secondaryPane, PaneContent primaryPane,
                           const ThreePaneScaffoldOptions& options) {
        auto& c = *rt::Composition::Current();
        ThreePaneScaffoldState* sp = &state;
        PaneExpansionState& expansion = options.paneExpansionState
                                            ? *options.paneExpansionState
                                            : RememberDefaultPaneExpansionState([sp] { return sp->TargetState().ExpansionStateKey(); },
                                                                                options.paneExpansionDragHandle != nullptr);
        const ui::LayoutDirection dir = c.Read(ui::LocalLayoutDirection);
        const ThreePaneScaffoldHorizontalOrder ltrOrder = paneOrder.ToLtrOrder(dir);
        const ThreePaneMotion paneMotions = CalculateThreePaneMotion(state, ltrOrder);
        auto& motion = c.Remember<std::shared_ptr<ThreePaneScaffoldMotionDataProvider>>([] { return std::make_shared<ThreePaneScaffoldMotionDataProvider>(); });
        motion->isPredictiveBackInProgress = [sp] { return sp->IsPredictiveBackInProgress(); };
        motion->Update(paneMotions, ltrOrder);
        auto& transition = state.RememberTransition();
        rt::SaveableStateHolder& holder = rt::RememberSaveableStateHolder();
        auto& focusRequesters = c.RememberNew<std::array<ui::FocusRequester, 3>>({}, rt::Key{});
        auto motionPtr = motion;
        PaneExpansionState* ex = &expansion;
        auto* trans = &transition;
        auto* holderPtr = &holder;
        auto* requesters = &focusRequesters;

        foundation::LookaheadScope([&c, sp, ex, motionPtr, trans, holderPtr, requesters, directive, ltrOrder, paneMotions, modifier, options, secondaryPane,
                                    primaryPane](ui::LookaheadScope& lookahead) {
            auto& scope = c.Remember<std::shared_ptr<ThreePaneScaffoldScope>>([] { return std::make_shared<ThreePaneScaffoldScope>(); });
            scope->m_Transition = trans;
            scope->m_State = sp;
            scope->m_Motion = motionPtr.get();
            scope->m_Lookahead = &lookahead;
            scope->m_Holder = holderPtr;
            scope->m_FocusRequesters = requesters;
            const ThreePaneScaffoldValue value = sp->TargetState();
            auto paneScope = [scope, &value, &paneMotions](Role role) {
                ThreePaneScaffoldPaneScope p;
                static_cast<ThreePaneScaffoldScope&>(p) = *scope;
                p.paneRole = role;
                p.paneMotion = paneMotions[role];
                p.isInteractable = value.IsInteractable(role);
                return p;
            };
            const auto primaryScope = paneScope(Role::Primary), secondaryScope = paneScope(Role::Secondary), tertiaryScope = paneScope(Role::Tertiary);
            std::vector<std::function<void()>> contents{
                [primaryPane, primaryScope] { if (primaryPane) primaryPane(primaryScope); },
                [secondaryPane, secondaryScope] { if (secondaryPane) secondaryPane(secondaryScope); },
                [tertiary = options.tertiaryPane, tertiaryScope] { if (tertiary) tertiary(tertiaryScope); },
                [handle = options.paneExpansionDragHandle, scope, ex] { if (handle) handle(*scope, *ex); },
                // A default scrim when no AnimatedPane shows one: the first pane's that levitates.
                [value] {
                    for (Role r : kRoles) {
                        const PaneAdaptedValue& v = value[r];
                        if (v.kind == Kind::Levitated && v.scrim && *v.scrim) (*v.scrim)();
                        return;   // as Compose: only the primary pane is looked at
                    }
                },
            };
            auto& policy = c.Remember<std::shared_ptr<ThreePanePolicy>>([&] { return std::make_shared<ThreePanePolicy>(ex, motionPtr.get()); },
                                                                       rt::Keys(reinterpret_cast<std::uintptr_t>(ex)));
            policy->directive.Set(directive);
            policy->value.Set(value);
            policy->order.Set(ltrOrder);
            CollectPredictiveBackScale(*sp, motionPtr->predictiveBackScaleState());
            PredictiveBackScaleState* backScale = &motionPtr->predictiveBackScaleState();
            Modifier m = GraphicsLayer(modifier, [backScale](GraphicsLayerParams& l) {
                const float s = backScale->Scale();
                l.scaleX = l.scaleY = s;
                l.originX = l.originY = 0.5f;
            });
            Layout(m, contents, policy);
            if (directive.shouldAutoFocusCurrentDestination) {
                const auto destination = value.currentDestination;
                auto* reqs = requesters;
                c.LaunchedEffect(rt::Keys(destination ? int(*destination) : -1), [destination, reqs]() -> rt::Task<> {
                    if (destination) (*reqs)[std::size_t(*destination)].RequestFocus();
                    co_return;
                });
            }
        });
    }

    void ThreePaneScaffold(const Modifier& modifier, const PaneScaffoldDirective& directive, const ThreePaneScaffoldValue& value,
                           const ThreePaneScaffoldHorizontalOrder& paneOrder, PaneContent secondaryPane, PaneContent primaryPane,
                           const ThreePaneScaffoldOptions& options) {
        auto& c = *rt::Composition::Current();
        auto& state = c.RememberNew<ThreePaneScaffoldState>({}, rt::Key{}, value);
        ThreePaneScaffoldState* sp = &state;
        // LaunchedEffect(scaffoldValue) { animateTo(scaffoldValue) }
        const ThreePaneScaffoldValue target = value;
        c.LaunchedEffect(rt::Keys(ValueHash(value)), [sp, target] { return sp->AnimateTo(target); });
        ThreePaneScaffold(modifier, directive, state, paneOrder, std::move(secondaryPane), std::move(primaryPane), options);
    }

    // --- List-detail and supporting-pane scaffolds -----------------------------------------------

    namespace ListDetailPaneScaffoldDefaults {
        ThreePaneScaffoldAdaptStrategies adaptStrategies(AdaptStrategy detail, AdaptStrategy list, AdaptStrategy extra) {
            return { std::move(detail), std::move(list), std::move(extra) };
        }
    }
    namespace SupportingPaneScaffoldDefaults {
        ThreePaneScaffoldAdaptStrategies adaptStrategies(AdaptStrategy main, AdaptStrategy supporting, AdaptStrategy extra) {
            return { std::move(main), std::move(supporting), std::move(extra) };
        }
    }

    namespace {
        ThreePaneScaffoldOptions OptionsFrom(const ListDetailPaneScaffoldOptions& o, PaneExpansionState& expansion) {
            ThreePaneScaffoldOptions t;
            t.tertiaryPane = o.extraPane;
            t.paneExpansionDragHandle = o.paneExpansionDragHandle;
            t.paneExpansionState = &expansion;
            return t;
        }
        PaneExpansionState& ExpansionFor(const ListDetailPaneScaffoldOptions& o, std::function<PaneExpansionStateKey()> key) {
            return o.paneExpansionState ? *o.paneExpansionState : RememberDefaultPaneExpansionState(std::move(key), o.paneExpansionDragHandle != nullptr);
        }
    }

    void ListDetailPaneScaffold(const PaneScaffoldDirective& directive, ThreePaneScaffoldState& state, PaneContent listPane, PaneContent detailPane,
                                const Modifier& modifier, const ListDetailPaneScaffoldOptions& options) {
        ThreePaneScaffoldState* sp = &state;
        PaneExpansionState& expansion = ExpansionFor(options, [sp] { return sp->TargetState().ExpansionStateKey(); });
        ThreePaneScaffold(FillMax(modifier, Axis::Both), directive, state, ListDetailPaneScaffoldDefaults::PaneOrder, std::move(listPane), std::move(detailPane),
                          OptionsFrom(options, expansion));
    }

    void ListDetailPaneScaffold(const PaneScaffoldDirective& directive, const ThreePaneScaffoldValue& value, PaneContent listPane, PaneContent detailPane,
                                const Modifier& modifier, const ListDetailPaneScaffoldOptions& options) {
        PaneExpansionState& expansion = ExpansionFor(options, [value] { return value.ExpansionStateKey(); });
        ThreePaneScaffold(FillMax(modifier, Axis::Both), directive, value, ListDetailPaneScaffoldDefaults::PaneOrder, std::move(listPane), std::move(detailPane),
                          OptionsFrom(options, expansion));
    }

    void SupportingPaneScaffold(const PaneScaffoldDirective& directive, ThreePaneScaffoldState& state, PaneContent mainPane, PaneContent supportingPane,
                                const Modifier& modifier, const SupportingPaneScaffoldOptions& options) {
        ThreePaneScaffoldState* sp = &state;
        PaneExpansionState& expansion = ExpansionFor(options, [sp] { return sp->TargetState().ExpansionStateKey(); });
        ThreePaneScaffold(FillMax(modifier, Axis::Both), directive, state, SupportingPaneScaffoldDefaults::PaneOrder, std::move(supportingPane), std::move(mainPane),
                          OptionsFrom(options, expansion));
    }

    void SupportingPaneScaffold(const PaneScaffoldDirective& directive, const ThreePaneScaffoldValue& value, PaneContent mainPane, PaneContent supportingPane,
                                const Modifier& modifier, const SupportingPaneScaffoldOptions& options) {
        PaneExpansionState& expansion = ExpansionFor(options, [value] { return value.ExpansionStateKey(); });
        ThreePaneScaffold(FillMax(modifier, Axis::Both), directive, value, SupportingPaneScaffoldDefaults::PaneOrder, std::move(supportingPane), std::move(mainPane),
                          OptionsFrom(options, expansion));
    }

}
