#include "kiln/material3/FloatingToolbar.h"
#include <kiln/Function.h>

#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draggable.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Transition.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/internal/ChildParentSemantics.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/tokens/ColorSchemeKeyTokens.h"
#include "kiln/material3/tokens/ElevationTokens.h"
#include "kiln/material3/tokens/FloatingToolbarTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;
    using Dir = FloatingToolbarExitDirection;

    namespace {
        Color Or(Color c, Color fallback) { return c.Specified() ? c : fallback; }
        Dp LerpDp(Dp a, Dp b, float t) { return Dp(a.value + (b.value - a.value) * t); }

        float CollapsedFraction(const FloatingToolbarState& s) {
            const float limit = s.OffsetLimit();
            return limit != 0 ? s.Offset() / limit : 0.0f;
        }

        // settleFloatingToolbar: a fling left over carries on with the decay, then the toolbar
        // snaps in or out, whichever is nearer. Returns the velocity left.
        rt::Task<float> Settle(std::shared_ptr<FloatingToolbarState> state, float velocity, anim::SpecPtr snap,
                               std::shared_ptr<const anim::DecaySpec> fling) {
            // Fully in or out (not 0: the fraction's float precision).
            const float fraction = CollapsedFraction(*state);
            if (fraction < 0.01f || fraction == 1.0f) co_return 0.0f;
            float remaining = velocity;
            if (fling && std::abs(velocity) > 1) {
                anim::Animatable a(0.0f);
                float last = 0;
                auto r = co_await a.AnimateDecay(velocity, fling, [&](float value) {
                    const float delta = value - last;
                    const float initial = state->Offset();
                    state->SetOffset(initial + delta);
                    const float consumed = std::abs(initial - state->Offset());
                    last = value;
                    // Stop once anything goes unconsumed (and against rounding).
                    if (std::abs(delta - consumed) > 0.5f) a.Stop();
                });
                remaining = r.reason == anim::EndReason::Finished ? 0.0f : r.endVelocity[0];
            }
            if (snap && state->Offset() < 0 && state->Offset() > state->OffsetLimit()) {
                anim::Animatable a(state->Offset());
                const float target = CollapsedFraction(*state) < 0.5f ? 0.0f : state->OffsetLimit();
                co_await a.AnimateTo(anim::Vec{ target, 0, 0, 0 }, snap, std::nullopt, [&](const anim::Vec& v) { state->SetOffset(v[0]); });
            }
            co_return remaining;
        }
        rt::Task<> SettleJob(std::shared_ptr<FloatingToolbarState> state, float velocity, anim::SpecPtr snap, std::shared_ptr<const anim::DecaySpec> fling) {
            co_await Settle(std::move(state), velocity, std::move(snap), std::move(fling));
        }

        // ExitAlwaysFloatingToolbarScrollBehavior.
        struct ExitAlways final : FloatingToolbarScrollBehavior, ui::NestedScrollConnection, std::enable_shared_from_this<ExitAlways> {
            Dir exitDirection = Dir::Bottom;
            std::shared_ptr<FloatingToolbarState> state;
            anim::SpecPtr snap;
            std::shared_ptr<const anim::DecaySpec> fling;
            std::unique_ptr<DraggableState> drag;
            bool isRtl = false;   // as last measured

            Dir ExitDirection() const override { return exitDirection; }
            FloatingToolbarState& State() const override { return *state; }
            anim::SpecPtr SnapAnimationSpec() const override { return snap; }
            std::shared_ptr<const anim::DecaySpec> FlingAnimationSpec() const override { return fling; }
            std::shared_ptr<ui::NestedScrollConnection> NestedScrollConnection() const override {
                return std::shared_ptr<ui::NestedScrollConnection>(std::const_pointer_cast<ExitAlways>(shared_from_this()), const_cast<ExitAlways*>(this));
            }
            bool Horizontal() const { return exitDirection == Dir::Start || exitDirection == Dir::End; }

            ui::Offset OnPostScroll(ui::Offset consumed, ui::Offset, ui::NestedScrollSource) override {
                state->SetContentOffset(state->ContentOffset() + consumed.y);
                state->SetOffset(state->Offset() + consumed.y);
                return {};
            }
            rt::Task<ui::Offset> OnPostFling(ui::Offset, ui::Offset available) override {
                // Scrolled all the way down: zero the total, against float drift.
                if (available.y > 0 && (state->Offset() == 0 || state->Offset() == state->OffsetLimit())) state->SetContentOffset(0);
                const float left = co_await Settle(state, available.y, snap, fling);
                co_return ui::Offset{ 0, left };
            }

            void Drag(float delta) {
                const float offset = Horizontal() && isRtl ? -delta : delta;
                if (exitDirection == Dir::Start || exitDirection == Dir::Top) state->SetOffset(state->Offset() + offset);
                else state->SetOffset(state->Offset() - offset);
            }

            ui::Modifier FloatingScrollBehavior(ui::Modifier m) const override {
                auto self = std::const_pointer_cast<ExitAlways>(shared_from_this());
                // The toolbar moves by the offset, towards its exit edge.
                m = LayoutMod(std::move(m), [self](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
                    self->isRtl = s.Direction() == ui::LayoutDirection::Rtl;
                    ui::Placeable& placeable = measurable.Measure(c);
                    const float offset = self->Horizontal() && self->isRtl ? -self->state->Offset() : self->state->Offset();
                    const int o = ui::RoundJava(offset);
                    const Dir dir = self->exitDirection;
                    return s.Layout(placeable.Width(), placeable.Height(), [&placeable, dir, o](ui::PlacementScope& place) {
                        switch (dir) {
                            case Dir::Start: place.Place(placeable, o, 0); break;
                            case Dir::End: place.Place(placeable, -o, 0); break;
                            case Dir::Top: place.Place(placeable, 0, o); break;
                            case Dir::Bottom: place.Place(placeable, 0, -o); break;
                        }
                    });
                });
                auto scope = internal::RememberCoroutineScope(rt::Key(7101));
                DraggableOptions o;
                o.onDragStopped = [self, scope](float velocity) { scope->Launch(SettleJob(self->state, velocity, self->snap, self->fling)); };
                m = Draggable(std::move(m), *drag, Horizontal() ? Orientation::Horizontal : Orientation::Vertical, o);
                // The limit: from where the toolbar sits to just past its parent's edge.
                return OnGloballyPositioned(std::move(m), [self](const LayoutCoordinates& coordinates) {
                    const ui::Offset parentOffset = coordinates.PositionInParent();
                    const ui::IntSize parentSize = coordinates.ParentSize();
                    const ui::IntSize size = coordinates.GetSize();
                    const bool rtl = self->isRtl;
                    float limit = 0;
                    switch (self->exitDirection) {
                        case Dir::Start: limit = rtl ? float(parentSize.width) - parentOffset.x : float(size.width) + parentOffset.x; break;
                        case Dir::End: limit = rtl ? float(size.width) + parentOffset.x : float(parentSize.width) - parentOffset.x; break;
                        case Dir::Top: limit = float(size.height) + parentOffset.y; break;
                        case Dir::Bottom: limit = float(parentSize.height) - parentOffset.y; break;
                    }
                    self->state->SetOffsetLimit(-(limit - self->state->Offset()));
                });
            }
        };

        // rememberTouchExplorationService.
        bool TouchExploration() {
            const auto manager = rt::Composition::Current()->Read(ui::LocalAccessibilityManager);
            return manager && ui::AccessibilityServiceOn(manager->ServiceState(), true, false, false);
        }

        // customToolbarActions: collapse while expanded, expand while collapsed.
        ui::Modifier ToolbarActions(ui::Modifier m, bool expanded, rt::State<bool>* forceCollapse) {
            std::string label = GetString(expanded ? Strings::FloatingToolbarCollapse : Strings::FloatingToolbarExpand);
            return internal::ParentSemantics(std::move(m), [expanded, forceCollapse, label](ui::SemanticsConfiguration& cfg) {
                cfg.customActions = { { label, [expanded, forceCollapse] {
                                           forceCollapse->Set(expanded);
                                           return true;
                                       } } };
            });
        }

        // minimumInteractiveBalancedPadding: without leading or trailing content, the touch
        // target padding along one axis is matched along the other (animated, so it bounces).
        ui::Modifier MinimumInteractiveBalancedPadding(ui::Modifier m, bool hasVisibleLeadingContent, bool hasVisibleTrailingContent,
                                                       anim::SpecPtr spec) {
            const float progress = AnimateFloatAsState(hasVisibleLeadingContent || hasVisibleTrailingContent ? 0.0f : 1.0f, std::move(spec), {},
                                                       rt::Key(7102));
            const bool both = hasVisibleLeadingContent && hasVisibleTrailingContent;
            return LayoutMod(std::move(m), [progress, both](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
                ui::Placeable& placeable = measurable.Measure(c);
                float vertical = 0, horizontal = 0;
                if (!both) {
                    const int top = placeable.Get(MinimumInteractiveTopAlignmentLine);
                    const int left = placeable.Get(MinimumInteractiveLeftAlignmentLine);
                    if (top != ui::AlignmentLine::Unspecified) vertical = float(top) * progress;
                    if (left != ui::AlignmentLine::Unspecified) horizontal = float(left) * progress;
                }
                const int width = placeable.Width() + ui::RoundJava(std::max(0.0f, (vertical - horizontal) * 2));
                const int height = placeable.Height() + ui::RoundJava(std::max(0.0f, (horizontal - vertical) * 2));
                return s.Layout(width, height, [&placeable, width, height](ui::PlacementScope& place) {
                    place.Place(placeable, (width - placeable.Width()) / 2, (height - placeable.Height()) / 2);
                });
            });
        }

        // The toolbar's shared state: touch exploration, the accessibility collapse, focus.
        struct ToolbarSetup {
            bool expanded;
            rt::State<bool>* forceCollapse;
            std::shared_ptr<FloatingToolbarScrollBehavior> behavior;
        };
        ToolbarSetup Setup(bool expanded, const std::shared_ptr<FloatingToolbarScrollBehavior>& behavior) {
            const bool touch = TouchExploration();
            auto& forceCollapse = rt::RememberSaveableState<bool>(false, rt::AutoSaver<bool>(), rt::Key(7103));
            return { !forceCollapse.Get() && (touch || expanded), &forceCollapse, touch ? nullptr : behavior };
        }
        // An offscreen toolbar is not keyboard focusable.
        ui::Modifier UnfocusableWhileMoved(ui::Modifier m, const std::shared_ptr<FloatingToolbarScrollBehavior>& behavior) {
            auto& c = *rt::Composition::Current();
            FloatingToolbarState* state = behavior ? &behavior->State() : nullptr;
            auto& shouldFocus = c.RememberNew<rt::Derived<bool>>(rt::Keys(state), {}, [state] { return !state || state->Offset() == 0; });
            if (shouldFocus.Get()) return m;
            return FocusPropertiesMod(std::move(m), [](ui::FocusProperties& p) { p.canFocus = false; });
        }

        struct LayoutParams {
            bool expanded;
            rt::State<bool>* forceCollapse;
            FloatingToolbarColors colors;
            PaddingValues contentPadding;
            std::shared_ptr<FloatingToolbarScrollBehavior> behavior;
            ShapePtr shape;
            kiln::Function<void()> leading, trailing;
            Dp expandedShadowElevation, collapsedShadowElevation;
        };

        // HorizontalFloatingToolbarLayout / VerticalFloatingToolbarLayout.
        void ToolbarLayout(bool vertical, const ui::Modifier& modifier, const LayoutParams& p, kiln::Function<void()> content) {
            auto& c = *rt::Composition::Current();
            const Dp shadow = AnimateDpAsState(p.expanded ? p.expandedShadowElevation : p.collapsedShadowElevation,
                                               FloatingToolbarDefaults::animationSpec(), {}, rt::Key(7104));
            ui::Modifier m = modifier;
            if (p.behavior) m = p.behavior->FloatingScrollBehavior(std::move(m));
            GraphicsLayerParams layer;
            layer.shadowElevation = c.Read(ui::LocalDensity).ToPx(shadow);
            layer.clipShape = p.shape;
            layer.clip = true;
            m = GraphicsLayer(std::move(m), layer);
            m = vertical ? SizeIn(std::move(m), FloatingToolbarDefaults::ContainerSize, Dp::Unspecified(), Dp::Unspecified(), Dp::Unspecified())
                         : SizeIn(std::move(m), Dp::Unspecified(), FloatingToolbarDefaults::ContainerSize, Dp::Unspecified(), Dp::Unspecified());
            m = Padding(Background(std::move(m), p.colors.toolbarContainerColor, p.shape), p.contentPadding);
            auto body = [p, vertical, content = std::move(content)] {
                rt::Composition::Current()->Provide(LocalContentColor, p.colors.toolbarContentColor, [=] {
                    const auto spec = FloatingToolbarDefaults::animationSpec();
                    if (p.leading) {
                        auto leading = p.leading;
                        if (vertical)
                            AnimatedVisibilityInColumn(p.expanded, {}, [leading] { Column({}, leading); },
                                                       FloatingToolbarDefaults::verticalEnterTransition(ui::AlignmentV::Bottom()),
                                                       FloatingToolbarDefaults::verticalExitTransition(ui::AlignmentV::Bottom()), rt::Key(1));
                        else
                            AnimatedVisibilityInRow(p.expanded, {}, [leading] { Row({}, leading); },
                                                    FloatingToolbarDefaults::horizontalEnterTransition(ui::AlignmentH::Start()),
                                                    FloatingToolbarDefaults::horizontalExitTransition(ui::AlignmentH::End()), rt::Key(1));
                    }
                    ui::Modifier inner = ToolbarActions({}, p.expanded, p.forceCollapse);
                    inner = MinimumInteractiveBalancedPadding(std::move(inner), p.expanded && p.leading != nullptr, p.expanded && p.trailing != nullptr,
                                                              Value(MotionSchemeKeyTokens::DefaultEffects));
                    if (vertical) Column(inner, content, rt::Key(2));
                    else Row(inner, content, rt::Key(2));
                    if (p.trailing) {
                        auto trailing = p.trailing;
                        if (vertical)
                            AnimatedVisibilityInColumn(p.expanded, {}, [trailing] { Column({}, trailing); },
                                                       FloatingToolbarDefaults::verticalEnterTransition(ui::AlignmentV::Top()),
                                                       FloatingToolbarDefaults::verticalExitTransition(ui::AlignmentV::Top()), rt::Key(3));
                        else
                            AnimatedVisibilityInRow(p.expanded, {}, [trailing] { Row({}, trailing); },
                                                    FloatingToolbarDefaults::horizontalEnterTransition(ui::AlignmentH::End()),
                                                    FloatingToolbarDefaults::horizontalExitTransition(ui::AlignmentH::Start()), rt::Key(3));
                    }
                });
            };
            if (vertical) Column(m, Arrangement::Center(), ui::AlignmentH::CenterHorizontally(), body);
            else Row(m, Arrangement::Center(), ui::AlignmentV::CenterVertically(), body);
        }

        struct FabLayoutParams {
            bool expanded;
            rt::State<bool>* forceCollapse;
            FloatingToolbarColors colors;
            PaddingValues contentPadding;
            std::shared_ptr<FloatingToolbarScrollBehavior> behavior;
            ShapePtr shape;
            bool fabAtEnd;   // End (horizontal) / Bottom (vertical)
            anim::SpecPtr spec;
            Dp expandedShadowElevation, collapsedShadowElevation;
        };

        // HorizontalFloatingToolbarWithFabLayout / VerticalFloatingToolbarWithFabLayout.
        void ToolbarWithFabLayout(bool vertical, const ui::Modifier& modifier, const FabLayoutParams& p, kiln::Function<void()> fab,
                                  kiln::Function<void()> toolbar) {
            auto& c = *rt::Composition::Current();
            auto& transition = UpdateTransition<float>(p.expanded ? 1.0f : 0.0f, rt::Key(7105));
            const anim::SpecPtr spec = p.spec;
            const float progress = AnimateFloat<float>(transition, [](const float& s) { return s; },
                                                       [spec](const Transition<float>::Segment&) { return spec; }, rt::Key(7106));
            const float density = c.Read(ui::LocalDensity).density;
            ui::Modifier m = vertical ? DefaultMinSize(modifier, FloatingToolbarDefaults::FabSizeMax, Dp::Unspecified())
                                      : DefaultMinSize(modifier, Dp::Unspecified(), FloatingToolbarDefaults::FabSizeMax);
            if (p.behavior) m = p.behavior->FloatingScrollBehavior(std::move(m));

            auto policy = std::make_shared<ui::LambdaMeasurePolicy>(
                [progress, vertical, fabAtEnd = p.fabAtEnd](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& cs) {
                    ui::Measurable& toolbarMeasurable = *children[0];
                    ui::Measurable& fabMeasurable = *children[1];
                    // The FAB is at its smallest when fully expanded.
                    const int fabSize = s.RoundToPx(LerpDp(FloatingToolbarDefaults::FabSizeMin, FloatingToolbarDefaults::FabSizeMax, 1 - progress));
                    ui::Placeable& fabPlaceable = fabMeasurable.Measure(ui::Constraints{ fabSize, fabSize, fabSize, fabSize });
                    const int containerSize = s.RoundToPx(FloatingToolbarDefaults::ContainerSize);
                    const int gap = s.RoundToPx(FloatingToolbarDefaults::ToolbarToFabGap);
                    const int fabMin = s.RoundToPx(FloatingToolbarDefaults::FabSizeMin);
                    if (!vertical) {
                        // The toolbar's widest, unrolled by the progress within the room left.
                        const int maxToolbarWidth = toolbarMeasurable.MaxIntrinsicWidth(containerSize);
                        const int targetWidth = int(float(maxToolbarWidth) * progress);
                        ui::Constraints tc = cs;
                        tc.maxWidth = std::clamp(targetWidth, 0, cs.maxWidth);
                        tc.minHeight = std::clamp(containerSize, 0, cs.minHeight);
                        tc.minWidth = std::min(tc.minWidth, tc.maxWidth);
                        ui::Placeable& toolbarPlaceable = toolbarMeasurable.Measure(tc);
                        const int width = maxToolbarWidth + gap + fabMin;
                        const int height = cs.minHeight;
                        const int toolbarTop = (height - toolbarPlaceable.Height()) / 2;
                        const int fabTop = (height - fabPlaceable.Height()) / 2;
                        const int fabX = fabAtEnd ? width - fabPlaceable.Width() : 0;
                        const int toolbarX = fabAtEnd ? maxToolbarWidth - toolbarPlaceable.Width() : width - maxToolbarWidth;
                        return s.Layout(width, height, [&, toolbarX, toolbarTop, fabX, fabTop](ui::PlacementScope& place) {
                            place.PlaceRelative(toolbarPlaceable, toolbarX, toolbarTop);
                            place.PlaceRelative(fabPlaceable, fabX, fabTop);
                        });
                    }
                    const int maxToolbarHeight = toolbarMeasurable.MaxIntrinsicHeight(containerSize);
                    const int targetHeight = int(std::max(0.0f, float(maxToolbarHeight) * progress));
                    ui::Constraints tc = cs;
                    tc.maxHeight = std::clamp(targetHeight, 0, cs.maxHeight);
                    tc.minWidth = std::clamp(containerSize, 0, cs.minWidth);
                    tc.minHeight = std::min(tc.minHeight, tc.maxHeight);
                    ui::Placeable& toolbarPlaceable = toolbarMeasurable.Measure(tc);
                    const int width = cs.minWidth;
                    const int height = maxToolbarHeight + gap + fabMin;
                    const int toolbarX = (width - toolbarPlaceable.Width()) / 2;
                    const int fabX = (width - fabPlaceable.Width()) / 2;
                    const int fabY = fabAtEnd ? height - fabPlaceable.Height() : 0;
                    const int toolbarY = fabAtEnd ? maxToolbarHeight - toolbarPlaceable.Height() : height - maxToolbarHeight;
                    return s.Layout(width, height, [&, toolbarX, toolbarY, fabX, fabY](ui::PlacementScope& place) {
                        place.PlaceRelative(toolbarPlaceable, toolbarX, toolbarY);
                        place.PlaceRelative(fabPlaceable, fabX, fabY);
                    });
                });

            Layout(m, policy, [p, vertical, progress, density, fab = std::move(fab), toolbar = std::move(toolbar)] {
                // placeRelativeWithLayer: the toolbar's elevation, shape and clip.
                GraphicsLayerParams layer;
                layer.shadowElevation = density * LerpDp(p.collapsedShadowElevation, p.expandedShadowElevation, std::min(progress, 1.0f)).value;
                layer.clipShape = p.shape;
                layer.clip = true;
                ui::Modifier bar = Padding(Background(GraphicsLayer({}, layer), p.colors.toolbarContainerColor), p.contentPadding);
                auto& scroll = RememberScrollState();
                auto body = [&] { rt::Composition::Current()->Provide(LocalContentColor, p.colors.toolbarContentColor, toolbar); };
                if (vertical) Column(VerticalScroll(std::move(bar), scroll), Arrangement::Top(), ui::AlignmentH::CenterHorizontally(), body);
                else Row(HorizontalScroll(std::move(bar), scroll), Arrangement::Start(), ui::AlignmentV::CenterVertically(), body);
                Box(ToolbarActions({}, p.expanded, p.forceCollapse), Alignment::TopStart(), false, fab);
            });
        }

        void FabWithElevation(kiln::Function<void()> onClick, const ui::Modifier& modifier, FloatingActionButtonOptions o, Color container,
                              Color content, kiln::Function<void()> body) {
            if (!o.containerColor) o.containerColor = container;
            if (!o.contentColor) o.contentColor = content;
            if (!o.elevation) o.elevation = FloatingActionButtonElevation{ ElevationTokens::Level2, ElevationTokens::Level2, ElevationTokens::Level2,
                                                                         ElevationTokens::Level3 };
            FloatingActionButton(std::move(onClick), FillMax(modifier, Axis::Both), o, std::move(body));
        }

        // VerticalNestedScrollExpansionNode.
        struct ExpansionConnection final : ui::NestedScrollConnection {
            bool expanded = false;
            kiln::Function<void()> onExpand, onCollapse;
            bool reverseLayout = false;
            Dp expandThreshold = Dp::Unspecified(), collapseThreshold = Dp::Unspecified();
            float expandPx = 0, collapsePx = 0;
            float contentOffset = 0, threshold = 0;
            bool attached = false;

            ui::Offset OnPostScroll(ui::Offset consumed, ui::Offset, ui::NestedScrollSource) override {
                // Reversed layouts scroll the other way.
                const float delta = consumed.y * (reverseLayout ? -1.0f : 1.0f);
                contentOffset += delta;
                if (delta < 0 && contentOffset <= threshold) {
                    threshold = contentOffset + expandPx;
                    if (onCollapse) onCollapse();
                } else if (delta > 0 && contentOffset >= threshold) {
                    threshold = contentOffset - collapsePx;
                    if (onExpand) onExpand();
                }
                return {};
            }
            void UpdateThreshold() { threshold = expanded ? contentOffset - collapsePx : contentOffset + expandPx; }
        };
    }

    // --- state, colours, defaults --------------------------------------------------------------

    FloatingToolbarColors FloatingToolbarColors::Copy(Color a, Color b, Color c, Color d) const {
        return { Or(a, toolbarContainerColor), Or(b, toolbarContentColor), Or(c, fabContainerColor), Or(d, fabContentColor) };
    }

    FloatingToolbarState::FloatingToolbarState(float limit, float offset, float content)
        : m_OffsetLimit(limit), m_Offset(offset), m_ContentOffset(content) {}
    void FloatingToolbarState::SetOffset(float offset) {
        // coerceIn(offsetLimit, 0f)
        const float limit = m_OffsetLimit.Peek();
        m_Offset.Set(limit <= 0 ? std::clamp(offset, limit, 0.0f) : 0.0f);
    }

    std::shared_ptr<FloatingToolbarState> RememberFloatingToolbarState(float limit, float offset, float content, rt::Key key) {
        rt::Saver<std::shared_ptr<FloatingToolbarState>> saver{
            [](const std::shared_ptr<FloatingToolbarState>& s) {
                return rt::SavedValue(rt::SavedValue::List{ rt::SavedValue(double(s->OffsetLimit())), rt::SavedValue(double(s->Offset())),
                                                           rt::SavedValue(double(s->ContentOffset())) });
            },
            [](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<FloatingToolbarState>> {
                const auto* list = std::get_if<rt::SavedValue::List>(&saved.v);
                if (!list || list->size() != 3) return std::nullopt;
                const auto a = rt::SavedNumber((*list)[0]), b = rt::SavedNumber((*list)[1]), c = rt::SavedNumber((*list)[2]);
                if (!a || !b || !c) return std::nullopt;
                return std::make_shared<FloatingToolbarState>(float(*a), float(*b), float(*c));
            }
        };
        return rt::RememberSaveable<std::shared_ptr<FloatingToolbarState>>(
            [=] { return std::make_shared<FloatingToolbarState>(limit, offset, content); }, saver, {}, key);
    }

    namespace FloatingToolbarDefaults {
        ShapePtr ContainerShape() { return Value(FloatingToolbarTokens::ContainerShape); }
        anim::SpecPtr animationSpec() { return Value(MotionSchemeKeyTokens::FastSpatial); }

        std::shared_ptr<FloatingToolbarScrollBehavior> exitAlwaysScrollBehavior(Dir exitDirection, std::shared_ptr<FloatingToolbarState> state,
                                                                                std::optional<anim::SpecPtr> snap,
                                                                                std::optional<std::shared_ptr<const anim::DecaySpec>> fling) {
            auto& c = *rt::Composition::Current();
            if (!state) state = RememberFloatingToolbarState();
            const anim::SpecPtr s = snap.value_or(Value(MotionSchemeKeyTokens::DefaultEffects));
            std::shared_ptr<const anim::DecaySpec> f;
            if (fling) f = *fling;
            else {
                // rememberSplineBasedDecay: the platform scroller's curve at this density.
                const float density = c.Read(ui::LocalDensity).density;
                f = c.Remember<std::shared_ptr<const anim::DecaySpec>>([density] { return std::make_shared<anim::SplineDecay>(density); },
                                                                       rt::Keys(density));
            }
            auto& b = c.Remember<std::shared_ptr<ExitAlways>>(
                [] {
                    auto behavior = std::make_shared<ExitAlways>();
                    std::weak_ptr<ExitAlways> w = behavior;
                    behavior->drag = std::make_unique<DraggableState>([w](float delta) { if (auto b = w.lock()) b->Drag(delta); });
                    return behavior;
                },
                rt::Keys(int(exitDirection), state.get(), s.get(), f.get()));
            b->exitDirection = exitDirection;
            b->state = state;
            b->snap = s;
            b->fling = f;
            return b;
        }

        EnterTransition horizontalEnterTransition(ui::AlignmentH expandFrom) { return ExpandHorizontally(animationSpec(), expandFrom); }
        EnterTransition verticalEnterTransition(ui::AlignmentV expandFrom) { return ExpandVertically(animationSpec(), expandFrom); }
        ExitTransition horizontalExitTransition(ui::AlignmentH shrinkTowards) { return ShrinkHorizontally(animationSpec(), shrinkTowards); }
        ExitTransition verticalExitTransition(ui::AlignmentV shrinkTowards) { return ShrinkVertically(animationSpec(), shrinkTowards); }

        FloatingToolbarColors standardFloatingToolbarColors() {
            const Color container = Value(FloatingToolbarTokens::StandardContainerColor);
            const Color fab = Value(ColorSchemeKeyTokens::PrimaryContainer);
            return { container, ContentColorFor(container), fab, ContentColorFor(fab) };
        }
        FloatingToolbarColors standardFloatingToolbarColors(Color a, Color b, Color c, Color d) { return standardFloatingToolbarColors().Copy(a, b, c, d); }
        FloatingToolbarColors vibrantFloatingToolbarColors() {
            const Color container = Value(FloatingToolbarTokens::VibrantContainerColor);
            const Color fab = Value(ColorSchemeKeyTokens::TertiaryContainer);
            return { container, ContentColorFor(container), fab, ContentColorFor(fab) };
        }
        FloatingToolbarColors vibrantFloatingToolbarColors(Color a, Color b, Color c, Color d) { return vibrantFloatingToolbarColors().Copy(a, b, c, d); }

        void VibrantFloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                         kiln::Function<void()> content) {
            const auto colors = vibrantFloatingToolbarColors();
            FabWithElevation(std::move(onClick), modifier, options, colors.fabContainerColor, colors.fabContentColor, std::move(content));
        }
        void StandardFloatingActionButton(kiln::Function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                          kiln::Function<void()> content) {
            const auto colors = standardFloatingToolbarColors();
            FabWithElevation(std::move(onClick), modifier, options, colors.fabContainerColor, colors.fabContentColor, std::move(content));
        }

        ui::Modifier FloatingToolbarVerticalNestedScroll(ui::Modifier m, bool expanded, kiln::Function<void()> onExpand, kiln::Function<void()> onCollapse,
                                                         Dp expandThreshold, Dp collapseThreshold, bool reverseLayout) {
            auto& c = *rt::Composition::Current();
            const auto density = c.Read(ui::LocalDensity);
            auto& connection = c.Remember<std::shared_ptr<ExpansionConnection>>([] { return std::make_shared<ExpansionConnection>(); }, {},
                                                                                 rt::Key(7107));
            auto& n = *connection;
            // onAttach / updateNode.
            if (!n.attached || n.expandThreshold != expandThreshold || n.collapseThreshold != collapseThreshold) {
                n.expandThreshold = expandThreshold;
                n.collapseThreshold = collapseThreshold;
                n.expandPx = density.ToPx(expandThreshold);
                n.collapsePx = density.ToPx(collapseThreshold);
                n.expanded = expanded;
                n.UpdateThreshold();
                n.attached = true;
            }
            n.reverseLayout = reverseLayout;
            n.onExpand = std::move(onExpand);
            n.onCollapse = std::move(onCollapse);
            if (n.expanded != expanded) {
                n.expanded = expanded;
                n.UpdateThreshold();
            }
            return NestedScroll(std::move(m), connection);
        }
    }

    // --- toolbars ----------------------------------------------------------------------------------

    namespace {
        void PlainToolbar(bool vertical, bool expanded, const ui::Modifier& modifier, const FloatingToolbarOptions& o, kiln::Function<void()> content) {
            const auto setup = Setup(expanded, o.scrollBehavior);
            LayoutParams p;
            p.expanded = setup.expanded;
            p.forceCollapse = setup.forceCollapse;
            p.colors = o.colors.value_or(FloatingToolbarDefaults::standardFloatingToolbarColors());
            p.contentPadding = o.contentPadding;
            p.behavior = setup.behavior;
            p.shape = o.shape ? o.shape : FloatingToolbarDefaults::ContainerShape();
            p.leading = o.leadingContent;
            p.trailing = o.trailingContent;
            p.expandedShadowElevation = o.expandedShadowElevation;
            p.collapsedShadowElevation = o.collapsedShadowElevation;
            // Focus follows the given behaviour, even when touch exploration pins the toolbar.
            ToolbarLayout(vertical, UnfocusableWhileMoved(modifier, o.scrollBehavior), p, std::move(content));
        }
        void ToolbarWithFab(bool vertical, bool expanded, kiln::Function<void()> fab, const ui::Modifier& modifier, const FloatingToolbarWithFabOptions& o,
                            kiln::Function<void()> content) {
            const auto setup = Setup(expanded, o.scrollBehavior);
            FabLayoutParams p;
            p.expanded = setup.expanded;
            p.forceCollapse = setup.forceCollapse;
            p.colors = o.colors.value_or(FloatingToolbarDefaults::standardFloatingToolbarColors());
            p.contentPadding = o.contentPadding;
            p.behavior = setup.behavior;
            p.shape = o.shape ? o.shape : FloatingToolbarDefaults::ContainerShape();
            p.fabAtEnd = vertical ? o.verticalFabPosition == FloatingToolbarVerticalFabPosition::Bottom
                                  : o.horizontalFabPosition == FloatingToolbarHorizontalFabPosition::End;
            p.spec = o.animationSpec ? o.animationSpec : FloatingToolbarDefaults::animationSpec();
            p.expandedShadowElevation = o.expandedShadowElevation;
            p.collapsedShadowElevation = o.collapsedShadowElevation;
            ToolbarWithFabLayout(vertical, modifier, p, std::move(fab), std::move(content));
        }
    }

    void HorizontalFloatingToolbar(bool expanded, const ui::Modifier& modifier, const FloatingToolbarOptions& options, kiln::Function<void()> content) {
        PlainToolbar(false, expanded, modifier, options, std::move(content));
    }
    void VerticalFloatingToolbar(bool expanded, const ui::Modifier& modifier, const FloatingToolbarOptions& options, kiln::Function<void()> content) {
        PlainToolbar(true, expanded, modifier, options, std::move(content));
    }
    void HorizontalFloatingToolbar(bool expanded, kiln::Function<void()> floatingActionButton, const ui::Modifier& modifier,
                                   const FloatingToolbarWithFabOptions& options, kiln::Function<void()> content) {
        ToolbarWithFab(false, expanded, std::move(floatingActionButton), modifier, options, std::move(content));
    }
    void VerticalFloatingToolbar(bool expanded, kiln::Function<void()> floatingActionButton, const ui::Modifier& modifier,
                                 const FloatingToolbarWithFabOptions& options, kiln::Function<void()> content) {
        ToolbarWithFab(true, expanded, std::move(floatingActionButton), modifier, options, std::move(content));
    }

}
