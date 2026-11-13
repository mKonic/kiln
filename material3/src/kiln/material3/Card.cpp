#include "kiln/material3/Card.h"
#include <kiln/Function.h>

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Compose.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/Helpers.h"
#include "kiln/material3/tokens/ColorSchemeKeyTokens.h"
#include "kiln/material3/tokens/ShapeKeyTokens.h"
#include "kiln/material3/tokens/FilledCardTokens.h"
#include "kiln/material3/tokens/ElevatedCardTokens.h"
#include "kiln/material3/tokens/OutlinedCardTokens.h"

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        Color Take(Color color, Color fallback) { return color.Specified() ? color : fallback; }
        struct Entry { Interaction::Kind kind; std::uint64_t id; bool operator==(const Entry&) const = default; };
        using Entries = std::vector<Entry>;
        rt::Task<> Animate(std::shared_ptr<anim::Animatable> value, CardElevation e, bool enabled, Dp target, std::optional<Interaction::Kind> to) {
            if (value->Target()[0] == target.value) co_return;
            if (!enabled) { value->SnapTo(target.value); co_return; }
            const float previous = value->Target()[0];
            std::optional<Interaction::Kind> from;
            if (previous == e.pressedElevation.value) from = Interaction::Kind::Press;
            else if (previous == e.hoveredElevation.value) from = Interaction::Kind::Hover;
            else if (previous == e.focusedElevation.value) from = Interaction::Kind::Focus;
            else if (previous == e.draggedElevation.value) from = Interaction::Kind::Drag;
            co_await internal::AnimateElevation(*value, target, from, to);
        }
        enum class Kind { Filled, Elevated, Outlined };
        CardColors Colors(Kind kind) {
            if (kind == Kind::Elevated) return CardDefaults::elevatedCardColors();
            if (kind == Kind::Outlined) return CardDefaults::outlinedCardColors();
            return CardDefaults::cardColors();
        }
        void Impl(Kind kind, bool clickable, kiln::Function<void()> click, const ui::Modifier& modifier, const CardOptions& options, kiln::Function<void()> content) {
            const bool enabled = !clickable || options.enabled;
            const auto colors = options.colors.value_or(Colors(kind));
            const auto elevation = options.elevation.value_or(kind == Kind::Elevated ? CardDefaults::elevatedCardElevation() :
                                                              kind == Kind::Outlined ? CardDefaults::outlinedCardElevation() : CardDefaults::cardElevation());
            const auto source = clickable ? (options.interactionSource ? options.interactionSource : RememberInteractionSource()) : nullptr;
            SurfaceOptions surface;
            surface.shape = options.shape ? options.shape : kind == Kind::Elevated ? CardDefaults::elevatedShape() :
                                                           kind == Kind::Outlined ? CardDefaults::outlinedShape() : CardDefaults::shape();
            surface.color = colors.ContainerColor(enabled);
            surface.contentColor = colors.ContentColor(enabled);
            surface.shadowElevation = ShadowElevation(elevation, enabled, source);
            surface.border = options.border ? *options.border : kind == Kind::Outlined ? std::optional(CardDefaults::outlinedCardBorder(enabled)) : std::nullopt;
            auto column = [content = std::move(content)] { Column({}, content); };
            if (clickable) Surface(std::move(click), modifier, surface, column, enabled, source);
            else Surface(modifier, surface, column);
        }
        CardColors OverrideColors(CardColors base, Color container, std::optional<Color> content, Color disabledContainer, std::optional<Color> disabledContent, bool outlined) {
            const auto resolved = content.value_or(ContentColorFor(container));
            const auto disabled = disabledContent.value_or((outlined ? ContentColorFor(container) : resolved).WithAlpha(0.38f));
            return base.Copy(container, resolved, disabledContainer, disabled);
        }
    }

    CardColors CardColors::Copy(Color container, Color content, Color disabledContainer, Color disabledContent) const {
        return { Take(container, containerColor), Take(content, contentColor), Take(disabledContainer, disabledContainerColor), Take(disabledContent, disabledContentColor) };
    }
    Dp ShadowElevation(const CardElevation& e, bool enabled, const std::shared_ptr<MutableInteractionSource>& source) {
        auto& c = *rt::Composition::Current();
        if (!source) return c.Remember<Dp>([&] { return e.defaultElevation; });
        auto& interactions = c.RememberState(Entries{});
        auto* state = &interactions;
        c.DisposableEffect(rt::Keys(std::hash<const void*>{}(source.get())), [source, state]() -> kiln::Function<void()> {
            const auto token = source->Subscribe([state](const Interaction& interaction) {
                auto entries = state->Peek();
                if (interaction.phase == Interaction::Phase::Start) entries.push_back({ interaction.kind, interaction.id });
                else std::erase_if(entries, [&](const Entry& entry) { return entry.id == interaction.id; });
                state->Set(std::move(entries));
            });
            return [source, token] { source->Unsubscribe(token); };
        });
        const auto& entries = interactions.Get();
        const auto interaction = entries.empty() ? std::nullopt : std::optional(entries.back().kind);
        Dp target = e.defaultElevation;
        if (!enabled) target = e.disabledElevation;
        else if (interaction == Interaction::Kind::Press) target = e.pressedElevation;
        else if (interaction == Interaction::Kind::Hover) target = e.hoveredElevation;
        else if (interaction == Interaction::Kind::Focus) target = e.focusedElevation;
        else if (interaction == Interaction::Kind::Drag) target = e.draggedElevation;
        auto value = c.Remember<std::shared_ptr<anim::Animatable>>([target] { return std::make_shared<anim::Animatable>(target.value); });
        c.LaunchedEffect(rt::Keys(target.value), [=] { return Animate(value, e, enabled, target, interaction); });
        return Dp(value->Float());
    }

    void Card(const ui::Modifier& m, const CardOptions& o, kiln::Function<void()> content) { Impl(Kind::Filled, false, {}, m, o, std::move(content)); }
    void Card(kiln::Function<void()> click, const ui::Modifier& m, const CardOptions& o, kiln::Function<void()> content) { Impl(Kind::Filled, true, std::move(click), m, o, std::move(content)); }
    void ElevatedCard(const ui::Modifier& m, const CardOptions& o, kiln::Function<void()> content) { Impl(Kind::Elevated, false, {}, m, o, std::move(content)); }
    void ElevatedCard(kiln::Function<void()> click, const ui::Modifier& m, const CardOptions& o, kiln::Function<void()> content) { Impl(Kind::Elevated, true, std::move(click), m, o, std::move(content)); }
    void OutlinedCard(const ui::Modifier& m, const CardOptions& o, kiln::Function<void()> content) { Impl(Kind::Outlined, false, {}, m, o, std::move(content)); }
    void OutlinedCard(kiln::Function<void()> click, const ui::Modifier& m, const CardOptions& o, kiln::Function<void()> content) { Impl(Kind::Outlined, true, std::move(click), m, o, std::move(content)); }

    namespace CardDefaults {
        ShapePtr shape() { return Value(FilledCardTokens::ContainerShape); }
        ShapePtr elevatedShape() { return Value(ElevatedCardTokens::ContainerShape); }
        ShapePtr outlinedShape() { return Value(OutlinedCardTokens::ContainerShape); }
        CardElevation cardElevation() {
            return { FilledCardTokens::ContainerElevation, FilledCardTokens::PressedContainerElevation, FilledCardTokens::FocusContainerElevation,
                     FilledCardTokens::HoverContainerElevation, FilledCardTokens::DraggedContainerElevation, FilledCardTokens::DisabledContainerElevation };
        }
        CardElevation elevatedCardElevation() {
            return { ElevatedCardTokens::ContainerElevation, ElevatedCardTokens::PressedContainerElevation, ElevatedCardTokens::FocusContainerElevation,
                     ElevatedCardTokens::HoverContainerElevation, ElevatedCardTokens::DraggedContainerElevation, ElevatedCardTokens::DisabledContainerElevation };
        }
        CardElevation outlinedCardElevation() {
            return { OutlinedCardTokens::ContainerElevation, OutlinedCardTokens::ContainerElevation, OutlinedCardTokens::ContainerElevation,
                     OutlinedCardTokens::ContainerElevation, OutlinedCardTokens::DraggedContainerElevation, OutlinedCardTokens::DisabledContainerElevation };
        }
        CardColors cardColors() {
            const auto container = Value(FilledCardTokens::ContainerColor), content = ContentColorFor(container);
            return { container, content, ui::CompositeOver(Value(FilledCardTokens::DisabledContainerColor).WithAlpha(FilledCardTokens::DisabledContainerOpacity), container), content.WithAlpha(0.38f) };
        }
        CardColors elevatedCardColors() {
            const auto container = Value(ElevatedCardTokens::ContainerColor), content = ContentColorFor(container), disabled = Value(ElevatedCardTokens::DisabledContainerColor);
            return { container, content, ui::CompositeOver(disabled.WithAlpha(ElevatedCardTokens::DisabledContainerOpacity), disabled), content.WithAlpha(0.38f) };
        }
        CardColors outlinedCardColors() {
            const auto container = Value(OutlinedCardTokens::ContainerColor), content = ContentColorFor(container);
            return { container, content, container, content.WithAlpha(0.38f) };
        }
        CardColors cardColors(Color c, std::optional<Color> text, Color disabled, std::optional<Color> disabledText) { return OverrideColors(cardColors(), c, text, disabled, disabledText, false); }
        CardColors elevatedCardColors(Color c, std::optional<Color> text, Color disabled, std::optional<Color> disabledText) { return OverrideColors(elevatedCardColors(), c, text, disabled, disabledText, false); }
        CardColors outlinedCardColors(Color c, std::optional<Color> text, Color disabled, std::optional<Color> disabledText) { return OverrideColors(outlinedCardColors(), c, text, disabled, disabledText, true); }
        BorderStroke outlinedCardBorder(bool enabled) {
            return { OutlinedCardTokens::OutlineWidth, enabled ? Value(OutlinedCardTokens::OutlineColor) :
                ui::CompositeOver(Value(OutlinedCardTokens::DisabledOutlineColor).WithAlpha(OutlinedCardTokens::DisabledOutlineOpacity), Value(ElevatedCardTokens::ContainerColor)) };
        }
    }
}
