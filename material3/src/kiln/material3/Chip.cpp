#include "kiln/material3/Chip.h"
#include <kiln/Function.h>

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/Helpers.h"
#include "kiln/material3/tokens/AssistChipTokens.h"
#include "kiln/material3/tokens/FilterChipTokens.h"
#include "kiln/material3/tokens/InputChipTokens.h"
#include "kiln/material3/tokens/SuggestionChipTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/ShapeKeyTokens.h"
#include "kiln/material3/tokens/TypographyKeyTokens.h"

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        using Content = kiln::Function<void()>;
        struct Entry { Interaction::Kind kind; std::uint64_t id; bool operator==(const Entry&) const = default; };
        struct ElevationState {
            anim::Animatable value;
            std::optional<Interaction::Kind> last;
            explicit ElevationState(float initial) : value(initial) {}
        };
        rt::Task<> AnimateElevation(std::shared_ptr<ElevationState> state, bool enabled, Dp target, std::optional<Interaction::Kind> to) {
            if (state->value.Target()[0] == target.value) co_return;
            if (!enabled) state->value.SnapTo(target.value);
            else co_await internal::AnimateElevation(state->value, target, state->last, to);
            state->last = to;
        }
        Content WithColor(Content content, Color color) {
            if (!content) return {};
            return [=] { rt::Composition::Current()->Provide(LocalContentColor, color, content); };
        }
        void RetainedIcon(Content icon) {
            auto& retained = rt::Composition::Current()->Remember<Content>([icon] { return icon; });
            if (icon) retained = std::move(icon);
            Box({}, ui::Alignment::Center(), false, [content = retained] { if (content) content(); });
        }
        struct ContentParams {
            Content label, leading, trailing;
            text::TextStyle textStyle;
            Color labelColor;
            Arrangement arrangement;
            PaddingValues padding;
            Dp height;
            bool animated = false, expressive = false;
        };
        void ChipContent(ContentParams p) {
            auto& c = *rt::Composition::Current();
            c.Provide(LocalContentColor, p.labelColor, [=, &c] {
                c.Provide(LocalTextStyle(), p.textStyle, [=] {
                    auto modifier = p.animated ? SizeIn({}, Dp::Unspecified(), Dp::Unspecified(), Dp(1000), Dp::Unspecified()) : Width({}, IntrinsicSize::Max);
                    modifier = Padding(DefaultMinSize(modifier, Dp::Unspecified(), p.height), p.padding);
                    Row(modifier, p.arrangement, ui::AlignmentV::CenterVertically(), [=] {
                        const auto enterAlpha = Value(p.expressive ? MotionSchemeKeyTokens::DefaultEffects : MotionSchemeKeyTokens::SlowEffects);
                        const auto exitAlpha = Value(p.expressive ? MotionSchemeKeyTokens::DefaultEffects : MotionSchemeKeyTokens::FastEffects);
                        // EnterExitLayout animates normalized progress, not IntSize pixels.
                        const auto expand = Value(MotionSchemeKeyTokens::FastSpatial);
                        const auto shrink = Value(p.expressive ? MotionSchemeKeyTokens::FastSpatial : MotionSchemeKeyTokens::DefaultEffects);
                        if (p.animated) {
                            Box({}, ui::Alignment::TopStart(), false, [=] {
                                AnimatedVisibilityInRow(bool(p.leading), {}, [=] { RetainedIcon(p.leading); },
                                                        ExpandHorizontally(expand, ui::AlignmentH::Start()) + FadeIn(enterAlpha),
                                                        ShrinkHorizontally(shrink, ui::AlignmentH::Start()) + FadeOut(exitAlpha));
                                if (!p.leading) Spacer(Width({}, Dp(0)));
                            });
                        } else if (p.leading) Box({}, ui::Alignment::Center(), false, p.leading);
                        else Spacer(Width({}, Dp(0)));
                        Row(Weight({}, 1, !p.animated), Arrangement::Start(), ui::AlignmentV::CenterVertically(), p.label);
                        if (p.animated) {
                            Box({}, ui::Alignment::TopStart(), false, [=] {
                                AnimatedVisibilityInRow(bool(p.trailing), {}, [=] { RetainedIcon(p.trailing); },
                                                        ExpandHorizontally(expand, ui::AlignmentH::End()) + FadeIn(enterAlpha),
                                                        ShrinkHorizontally(shrink, ui::AlignmentH::End()) + FadeOut(exitAlpha));
                                if (!p.trailing) Spacer(Width({}, Dp(0)));
                            });
                        } else if (p.trailing) Box({}, ui::Alignment::Center(), false, p.trailing);
                        else Spacer(Width({}, Dp(0)));
                    });
                });
            });
        }
        ShapePtr ShapeByInteraction(const ChipShapes& shapes, bool selected, bool pressed) {
            const auto press = shapes.pressedShape ? shapes.pressedShape : shapes.selectedShape;
            const auto target = pressed ? press : selected ? shapes.selectedShape : shapes.shape;
            if (!std::dynamic_pointer_cast<const CornerBasedShape>(shapes.shape) || !std::dynamic_pointer_cast<const CornerBasedShape>(shapes.selectedShape) || !std::dynamic_pointer_cast<const CornerBasedShape>(press)) return target;
            struct Identity { ChipShapes shapes; std::size_t version = 0; };
            auto& identity = rt::Composition::Current()->Remember<Identity>([shapes] { return Identity{ shapes }; });
            if (!(identity.shapes == shapes)) { identity.shapes = shapes; ++identity.version; }
            return internal::RememberAnimatedShape(std::dynamic_pointer_cast<const CornerBasedShape>(target), Value(MotionSchemeKeyTokens::FastSpatial), identity.version);
        }
        void Action(bool assist, bool elevated, Content click, Content label, const ui::Modifier& modifier, const ChipOptions& o) {
            const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
            const auto colors = o.colors.value_or(assist ? (elevated ? AssistChipDefaults::elevatedAssistChipColors() : AssistChipDefaults::assistChipColors()) :
                                                          (elevated ? SuggestionChipDefaults::elevatedSuggestionChipColors() : SuggestionChipDefaults::suggestionChipColors()));
            const auto elevation = o.elevation ? *o.elevation : std::optional(assist ? (elevated ? AssistChipDefaults::elevatedAssistChipElevation() : AssistChipDefaults::assistChipElevation()) :
                                                                                      (elevated ? SuggestionChipDefaults::elevatedSuggestionChipElevation() : SuggestionChipDefaults::suggestionChipElevation()));
            SurfaceOptions surface;
            surface.shape = o.shape ? o.shape : assist ? AssistChipDefaults::shape() : SuggestionChipDefaults::shape();
            surface.color = colors.ContainerColor(o.enabled);
            surface.shadowElevation = elevation ? ShadowElevation(*elevation, o.enabled, source) : Dp(0);
            surface.border = o.border ? *o.border : elevated ? std::nullopt : std::optional(assist ? AssistChipDefaults::assistChipBorder(o.enabled) : SuggestionChipDefaults::suggestionChipBorder(o.enabled));
            ContentParams p{ std::move(label), WithColor(o.leadingIcon, colors.LeadingIconContentColor(o.enabled)), assist ? WithColor(o.trailingIcon, colors.TrailingIconContentColor(o.enabled)) : Content{},
                             Value(assist ? AssistChipTokens::LabelTextFont : SuggestionChipTokens::LabelTextFont), colors.LabelColor(o.enabled),
                             o.horizontalArrangement.value_or(assist ? AssistChipDefaults::horizontalArrangement() : SuggestionChipDefaults::horizontalArrangement()),
                             o.contentPadding.value_or(assist ? AssistChipDefaults::ContentPadding : SuggestionChipDefaults::ContentPadding), assist ? AssistChipDefaults::Height : SuggestionChipDefaults::Height };
            Surface(std::move(click), Semantics(modifier, [](ui::SemanticsConfiguration& config) { config.role = ui::Role::Button; }), surface, [p] { ChipContent(p); }, o.enabled, source);
        }
        void Selectable(bool input, bool elevated, bool selected, Content click, Content label, const ui::Modifier& modifier, const SelectableChipOptions& o) {
            const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
            const auto colors = o.colors.value_or(input ? (o.shapes ? InputChipDefaults::tonalInputChipColors() : InputChipDefaults::inputChipColors()) :
                elevated ? (o.shapes ? FilterChipDefaults::tonalElevatedFilterChipColors() : FilterChipDefaults::elevatedFilterChipColors()) :
                           (o.shapes ? FilterChipDefaults::tonalFilterChipColors() : FilterChipDefaults::filterChipColors()));
            const auto elevation = o.elevation ? *o.elevation : std::optional(input ? InputChipDefaults::inputChipElevation() :
                elevated ? FilterChipDefaults::elevatedFilterChipElevation() : FilterChipDefaults::filterChipElevation());
            SurfaceOptions surface;
            surface.shape = o.shapes ? ShapeByInteraction(*o.shapes, selected, source->IsPressed()) : o.shape ? o.shape : input ? InputChipDefaults::shape() : FilterChipDefaults::shape();
            surface.color = colors.ContainerColor(o.enabled, selected);
            surface.shadowElevation = elevation ? ShadowElevation(*elevation, o.enabled, source) : Dp(0);
            surface.border = o.border ? *o.border : input ? std::optional(InputChipDefaults::inputChipBorder(o.enabled, selected)) :
                elevated ? std::nullopt : std::optional(FilterChipDefaults::filterChipBorder(o.enabled, selected));
            Content leading;
            if (input && o.avatar) {
                leading = [=] {
                    GraphicsLayerParams layer;
                    layer.alpha = o.enabled ? 1 : InputChipTokens::DisabledAvatarOpacity;
                    layer.clip = true;
                    layer.clipShape = Value(InputChipTokens::AvatarShape);
                    Box(GraphicsLayer({}, layer), ui::Alignment::Center(), false, o.avatar);
                };
            } else leading = WithColor(o.leadingIcon, colors.LeadingIconContentColor(o.enabled, selected));
            const bool avatar = input && bool(o.avatar), hasLeading = bool(o.leadingIcon), hasTrailing = bool(o.trailingIcon);
            const auto arrangement = o.horizontalArrangement.value_or(input ? (o.shapes ? InputChipDefaults::horizontalArrangement(avatar, hasLeading, hasTrailing) : InputChipDefaults::horizontalArrangement()) :
                o.shapes ? FilterChipDefaults::horizontalArrangement(hasLeading, hasTrailing) : FilterChipDefaults::horizontalArrangement());
            ContentParams p{ std::move(label), leading, WithColor(o.trailingIcon, colors.TrailingIconContentColor(o.enabled, selected)),
                             Value(input ? InputChipTokens::LabelTextFont : FilterChipTokens::LabelTextFont), colors.LabelColor(o.enabled, selected), arrangement,
                             o.contentPadding.value_or(input ? InputChipDefaults::contentPadding(avatar, hasLeading, hasTrailing) : FilterChipDefaults::ContentPadding),
                             input ? InputChipDefaults::Height : FilterChipDefaults::Height, true, bool(o.shapes) };
            Surface(selected, std::move(click), Semantics(modifier, [](ui::SemanticsConfiguration& config) { config.role = ui::Role::Checkbox; }), surface, [p] { ChipContent(p); }, o.enabled, source);
        }
    }

    Dp ShadowElevation(const ChipElevation& elevation, bool enabled, const std::shared_ptr<MutableInteractionSource>& source) {
        auto& c = *rt::Composition::Current();
        auto& entries = c.RememberState(std::vector<Entry>{});
        auto* interactions = &entries;
        c.DisposableEffect(rt::Keys(std::hash<const void*>{}(source.get())), [source, interactions]() -> Content {
            if (!source) return {};
            const auto token = source->Subscribe([interactions](const Interaction& interaction) {
                auto list = interactions->Peek();
                if (interaction.phase == Interaction::Phase::Start) list.push_back({ interaction.kind, interaction.id });
                else std::erase_if(list, [&](const Entry& entry) { return entry.id == interaction.id; });
                interactions->Set(std::move(list));
            });
            return [source, token] { source->Unsubscribe(token); };
        });
        const auto& list = entries.Get();
        const auto interaction = list.empty() ? std::nullopt : std::optional(list.back().kind);
        Dp target = !enabled ? elevation.disabledElevation : interaction == Interaction::Kind::Press ? elevation.pressedElevation :
            interaction == Interaction::Kind::Hover ? elevation.hoveredElevation : interaction == Interaction::Kind::Focus ? elevation.focusedElevation :
            interaction == Interaction::Kind::Drag ? elevation.draggedElevation : elevation.elevation;
        auto state = c.Remember<std::shared_ptr<ElevationState>>([target] { return std::make_shared<ElevationState>(target.value); });
        c.LaunchedEffect(rt::Keys(target.value), [=] { return AnimateElevation(state, enabled, target, interaction); });
        return Dp(state->value.Float());
    }

    void AssistChip(Content click, Content label, const ui::Modifier& modifier, const ChipOptions& o) { Action(true, false, std::move(click), std::move(label), modifier, o); }
    void ElevatedAssistChip(Content click, Content label, const ui::Modifier& modifier, const ChipOptions& o) { Action(true, true, std::move(click), std::move(label), modifier, o); }
    void SuggestionChip(Content click, Content label, const ui::Modifier& modifier, const ChipOptions& o) { Action(false, false, std::move(click), std::move(label), modifier, o); }
    void ElevatedSuggestionChip(Content click, Content label, const ui::Modifier& modifier, const ChipOptions& o) { Action(false, true, std::move(click), std::move(label), modifier, o); }
    void FilterChip(bool selected, Content click, Content label, const ui::Modifier& modifier, const SelectableChipOptions& o) { Selectable(false, false, selected, std::move(click), std::move(label), modifier, o); }
    void ElevatedFilterChip(bool selected, Content click, Content label, const ui::Modifier& modifier, const SelectableChipOptions& o) { Selectable(false, true, selected, std::move(click), std::move(label), modifier, o); }
    void InputChip(bool selected, Content click, Content label, const ui::Modifier& modifier, const SelectableChipOptions& o) { Selectable(true, false, selected, std::move(click), std::move(label), modifier, o); }
}
