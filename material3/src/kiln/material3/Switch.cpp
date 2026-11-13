#include "kiln/material3/Switch.h"
#include <kiln/Function.h>

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/AnchoredDraggable.h"
#include "kiln/foundation/Border.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/tokens/ColorSchemeKeyTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/ShapeKeyTokens.h"
#include "kiln/material3/tokens/SwitchTokens.h"

#include <cmath>
#include <limits>

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        Color Take(Color color, Color fallback) { return color.Specified() ? color : fallback; }
        struct ThumbParams {
            std::shared_ptr<MutableInteractionSource> source;
            bool checked;
            std::shared_ptr<AnchoredDraggableState<bool>> drag;
            anim::SpecPtr spec;
            bool operator==(const ThumbParams&) const = default;
        };
        rt::Task<> Animate(anim::Animatable& value, float target, anim::SpecPtr spec) { co_await value.AnimateTo(target, std::move(spec)); }
        // ThumbNode: the thumb's size (larger while pressed or dragged, animated with the motion
        // scheme) and its place, the drag state's offset along the track.
        struct ThumbNode final : ui::ModifierNode, ui::LayoutModifier {
            ThumbParams params;
            std::shared_ptr<MutableInteractionSource> subscribed;
            std::uint64_t token = 0;
            int pressCount = 0, dragCount = 0;
            float initialSize = std::numeric_limits<float>::quiet_NaN();
            std::unique_ptr<anim::Animatable> sizeAnim;
            rt::Job sizeJob;
            explicit ThumbNode(ThumbParams p) : params(std::move(p)) {}
            ui::LayoutModifier* AsLayout() override { return this; }
            void OnAttach() override { Subscribe(); }
            void OnDetach() override {
                sizeJob.Cancel();
                if (subscribed) subscribed->Unsubscribe(token);
                subscribed.reset(); pressCount = dragCount = 0;
            }
            // restartInteractionCollection: presses and drags both enlarge the thumb.
            void Subscribe() {
                if (subscribed == params.source) return;
                if (subscribed) subscribed->Unsubscribe(token);
                subscribed = params.source; pressCount = dragCount = 0;
                if (subscribed) token = subscribed->Subscribe([this](const Interaction& interaction) {
                    int* count = interaction.kind == Interaction::Kind::Press ? &pressCount : interaction.kind == Interaction::Kind::Drag ? &dragCount : nullptr;
                    if (!count) return;
                    const bool was = pressCount > 0 || dragCount > 0;
                    *count += interaction.phase == Interaction::Phase::Start ? 1 : -1;
                    if (was != (pressCount > 0 || dragCount > 0)) InvalidateMeasure();
                });
            }
            void OnParamsChanged() {
                if (Owner() && Owner()->GetOwner()) Subscribe();
                if (!sizeAnim && !std::isnan(initialSize)) sizeAnim = std::make_unique<anim::Animatable>(initialSize);
                InvalidateMeasure();
            }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                const bool hasContent = m.MaxIntrinsicHeight(c.maxWidth) != 0 && m.MaxIntrinsicWidth(c.maxHeight) != 0;
                const bool enlarged = pressCount > 0 || dragCount > 0;
                const Dp diameter = enlarged ? SwitchTokens::PressedHandleWidth : hasContent || params.checked ? SwitchTokens::SelectedHandleWidth : SwitchTokens::UnselectedHandleWidth;
                const float size = s.ToPx(diameter);
                if (!sizeAnim && !std::isnan(initialSize)) sizeAnim = std::make_unique<anim::Animatable>(initialSize);
                const int actual = std::max(0, int(sizeAnim ? sizeAnim->Float() : size));
                auto& p = m.Measure(ui::Constraints::Fixed(actual, actual));
                if (sizeAnim && sizeAnim->Target()[0] != size) {
                    sizeJob.Cancel();
                    sizeJob = rt::Launch(rt::Dispatcher::Main(), Animate(*sizeAnim, size, params.spec));
                }
                if (std::isnan(initialSize)) initialSize = size;
                const float minCenterX = s.ToPx(SwitchTokens::TrackHeight) / 2;
                const float travel = s.ToPx(Dp(SwitchTokens::TrackWidth.value - SwitchTokens::TrackHeight.value));
                return s.Layout(actual, actual, [this, &p, size, minCenterX, travel](ui::PlacementScope& ps) {
                    // The thumb's centre follows the drag offset (read here: a drag only places it again).
                    const float dragged = params.drag ? params.drag->Offset() : std::numeric_limits<float>::quiet_NaN();
                    const float offset = !std::isnan(dragged) ? dragged : params.checked ? travel : 0.0f;
                    const float current = sizeAnim ? sizeAnim->Float() : size;
                    ps.PlaceRelative(p, int(minCenterX + offset - current / 2), 0);
                });
            }
        };
        // The latest checked value and callback, for the effects below (rememberUpdatedState).
        struct SwitchLatest {
            bool checked = false;
            kiln::Function<void(bool)> change;
            rt::State<bool> forceAnimationCheck{ false };
        };
    }

    SwitchColors SwitchColors::Copy(const SwitchColors& o) const {
        return { Take(o.checkedThumbColor, checkedThumbColor), Take(o.checkedTrackColor, checkedTrackColor),
                 Take(o.checkedBorderColor, checkedBorderColor), Take(o.checkedIconColor, checkedIconColor),
                 Take(o.uncheckedThumbColor, uncheckedThumbColor), Take(o.uncheckedTrackColor, uncheckedTrackColor),
                 Take(o.uncheckedBorderColor, uncheckedBorderColor), Take(o.uncheckedIconColor, uncheckedIconColor),
                 Take(o.disabledCheckedThumbColor, disabledCheckedThumbColor), Take(o.disabledCheckedTrackColor, disabledCheckedTrackColor),
                 Take(o.disabledCheckedBorderColor, disabledCheckedBorderColor), Take(o.disabledCheckedIconColor, disabledCheckedIconColor),
                 Take(o.disabledUncheckedThumbColor, disabledUncheckedThumbColor), Take(o.disabledUncheckedTrackColor, disabledUncheckedTrackColor),
                 Take(o.disabledUncheckedBorderColor, disabledUncheckedBorderColor), Take(o.disabledUncheckedIconColor, disabledUncheckedIconColor) };
    }
    Color SwitchColors::ThumbColor(bool enabled, bool checked) const { return enabled ? (checked ? checkedThumbColor : uncheckedThumbColor) : (checked ? disabledCheckedThumbColor : disabledUncheckedThumbColor); }
    Color SwitchColors::TrackColor(bool enabled, bool checked) const { return enabled ? (checked ? checkedTrackColor : uncheckedTrackColor) : (checked ? disabledCheckedTrackColor : disabledUncheckedTrackColor); }
    Color SwitchColors::BorderColor(bool enabled, bool checked) const { return enabled ? (checked ? checkedBorderColor : uncheckedBorderColor) : (checked ? disabledCheckedBorderColor : disabledUncheckedBorderColor); }
    Color SwitchColors::IconColor(bool enabled, bool checked) const { return enabled ? (checked ? checkedIconColor : uncheckedIconColor) : (checked ? disabledCheckedIconColor : disabledUncheckedIconColor); }

    namespace SwitchDefaults {
        SwitchColors colors() {
            const auto surface = MaterialTheme::colorScheme().surface;
            auto disabled = [surface](ColorToken color, float alpha) { return ui::CompositeOver(Value(color).WithAlpha(alpha), surface); };
            return { Value(SwitchTokens::SelectedHandleColor), Value(SwitchTokens::SelectedTrackColor), Color::Transparent(), Value(SwitchTokens::SelectedIconColor),
                     Value(SwitchTokens::UnselectedHandleColor), Value(SwitchTokens::UnselectedTrackColor), Value(SwitchTokens::UnselectedFocusTrackOutlineColor), Value(SwitchTokens::UnselectedIconColor),
                     disabled(SwitchTokens::DisabledSelectedHandleColor, SwitchTokens::DisabledSelectedHandleOpacity),
                     disabled(SwitchTokens::DisabledSelectedTrackColor, SwitchTokens::DisabledTrackOpacity), Color::Transparent(),
                     disabled(SwitchTokens::DisabledSelectedIconColor, SwitchTokens::DisabledSelectedIconOpacity),
                     disabled(SwitchTokens::DisabledUnselectedHandleColor, SwitchTokens::DisabledUnselectedHandleOpacity),
                     disabled(SwitchTokens::DisabledUnselectedTrackColor, SwitchTokens::DisabledTrackOpacity),
                     disabled(SwitchTokens::DisabledUnselectedTrackOutlineColor, SwitchTokens::DisabledTrackOpacity),
                     disabled(SwitchTokens::DisabledUnselectedIconColor, SwitchTokens::DisabledUnselectedIconOpacity) };
        }
        SwitchColors colors(const SwitchColors& overrides) { return colors().Copy(overrides); }
    }

    void Switch(bool checked, kiln::Function<void(bool)> change, const ui::Modifier& modifier, const SwitchOptions& options) {
        const auto source = options.interactionSource ? options.interactionSource : RememberInteractionSource();
        const auto colors = options.colors.value_or(SwitchDefaults::colors());
        const auto thumbShape = Value(SwitchTokens::HandleShape), trackShape = Value(SwitchTokens::TrackShape);
        const auto iconColor = colors.IconColor(options.enabled, checked);
        const bool inset = bool(rt::Composition::Current()->Read(LocalRippleThemeConfiguration).focus);
        auto& c = *rt::Composition::Current();
        // Dragging the thumb (AnchoredDraggableState between unchecked at 0 and checked at the
        // track's travel); the settled value reports a change, and a new `checked` animates there.
        const anim::SpecPtr spec = Value(MotionSchemeKeyTokens::FastSpatial);
        const float travel = c.Read(ui::LocalDensity).ToPx(Dp(SwitchTokens::TrackWidth.value - SwitchTokens::TrackHeight.value));
        auto drag = c.Remember<std::shared_ptr<AnchoredDraggableState<bool>>>(
            [&] { return std::make_shared<AnchoredDraggableState<bool>>(checked, DraggableAnchors<bool>{ { false, 0.0f }, { true, travel } }); },
            rt::Keys(travel));
        auto latest = c.Remember<std::shared_ptr<SwitchLatest>>([] { return std::make_shared<SwitchLatest>(); });
        latest->checked = checked;
        latest->change = change;
        const bool settled = drag->SettledValue();
        c.LaunchedEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(drag.get()), settled), [latest, settled]() -> rt::Task<> {
            if (latest->checked != settled && latest->change) {
                latest->change(settled);
                latest->forceAnimationCheck.Set(!latest->forceAnimationCheck.Peek());
            }
            co_return;
        });
        const bool force = latest->forceAnimationCheck.Get();
        c.LaunchedEffect(rt::Keys(checked, force), [drag, checked, spec]() -> rt::Task<> {
            if (checked != drag->SettledValue()) co_await drag->AnimateTo(checked, spec);
        });
        auto m = modifier;
        if (change) {
            ClickableOptions toggle;
            toggle.enabled = options.enabled;
            toggle.role = ui::Role::Switch;
            toggle.interactionSource = source;
            toggle.useLocalIndication = false;
            m = Toggleable(MinimumInteractiveComponentSize(m), checked, std::move(change), toggle);
            AnchoredDraggableOptions dragOptions;
            dragOptions.enabled = options.enabled;
            dragOptions.interactionSource = source;
            dragOptions.snapAnimationSpec = spec;   // AnchoredDraggableDefaults.flingBehavior(state, animationSpec)
            m = AnchoredDraggable(std::move(m), *drag, Orientation::Horizontal, dragOptions);
        }
        m = RequiredSize(WrapContent(m, Axis::Both), SwitchTokens::TrackWidth, SwitchTokens::TrackHeight);
        m = Background(Border(m, SwitchTokens::TrackOutlineWidth, colors.BorderColor(options.enabled, checked), trackShape), colors.TrackColor(options.enabled, checked), trackShape);
        if (inset) {
            RippleOptions focus;
            focus.focusRingShape = trackShape;
            focus.enablePressIndication = focus.enableDragIndication = focus.enableHoverIndication = false;
            m = Ripple(focus)->Create(m, source);
        }
        Box(m, ui::Alignment::TopStart(), false, [=] {
            auto thumb = AlignInBox({}, ui::Alignment::CenterStart()).With<ThumbNode>(ThumbParams{ source, checked, drag, spec });
            RippleOptions ripple;
            ripple.bounded = false;
            ripple.radius = Dp(SwitchTokens::StateLayerSize.value / 2);
            ripple.enableFocusIndication = !inset;
            thumb = Background(Ripple(ripple)->Create(thumb, source), colors.ThumbColor(options.enabled, checked), thumbShape);
            Box(thumb, ui::Alignment::Center(), false, [=] {
                if (options.thumbContent) rt::Composition::Current()->Provide(LocalContentColor, iconColor, options.thumbContent);
            });
        });
    }
}
