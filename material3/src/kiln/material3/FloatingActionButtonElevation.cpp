#include "kiln/material3/FloatingActionButton.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Compose.h"
#include "kiln/material3/internal/Helpers.h"

namespace kiln::material3 {
    namespace {
        using foundation::Interaction;
        struct Entry { Interaction::Kind kind; std::uint64_t id; };
        struct ElevationState {
            explicit ElevationState(FloatingActionButtonElevation e) : values(e), animatable(e.defaultElevation.value) {}
            FloatingActionButtonElevation values;
            anim::Animatable animatable;
            std::vector<Entry> interactions;
            std::optional<Interaction::Kind> lastTarget, targetInteraction;
            rt::Job animation;
        };
        ui::Dp Target(const FloatingActionButtonElevation& e, std::optional<Interaction::Kind> kind) {
            if (kind == Interaction::Kind::Press) return e.pressedElevation;
            if (kind == Interaction::Kind::Hover) return e.hoveredElevation;
            if (kind == Interaction::Kind::Focus) return e.focusedElevation;
            return e.defaultElevation;
        }
        rt::Task<> Update(std::shared_ptr<ElevationState> state, FloatingActionButtonElevation values) {
            state->values = values;
            const auto target = Target(values, state->targetInteraction);
            if (state->animatable.Target()[0] != target.value) {
                state->animation.Cancel();
                state->animatable.SnapTo(target.value);
                state->lastTarget = state->targetInteraction;
            }
            co_return;
        }
        rt::Task<> Animate(ElevationState& state, std::optional<Interaction::Kind> to) {
            // Kotlin's finally also runs on interruption. Remember the interaction itself,
            // not a guess from the dp value (press/focus can have the same elevation).
            struct Finally {
                ElevationState& state;
                std::optional<Interaction::Kind> to;
                ~Finally() { state.lastTarget = to; }
            } finally{ state, to };
            co_await internal::AnimateElevation(state.animatable, Target(state.values, to), state.lastTarget, to);
        }
        void OnInteraction(ElevationState& state, const Interaction& interaction) {
            if (interaction.kind != Interaction::Kind::Press && interaction.kind != Interaction::Kind::Hover && interaction.kind != Interaction::Kind::Focus) return;
            if (interaction.phase == Interaction::Phase::Start) state.interactions.push_back({ interaction.kind, interaction.id });
            else std::erase_if(state.interactions, [&](const Entry& entry) { return entry.id == interaction.id; });
            const auto to = state.interactions.empty() ? std::nullopt : std::optional(state.interactions.back().kind);
            state.targetInteraction = to;
            // A new interaction with an equal target must not cancel the animation already
            // heading there. Upstream launches independently and Animatable interrupts only
            // when animateTo/snapTo is actually called.
            if (state.animatable.Target()[0] != Target(state.values, to).value) {
                state.animation.Cancel();
                state.animation = rt::Launch(rt::Dispatcher::Main(), Animate(state, to));
            } else state.lastTarget = to;
        }
    }

    ui::Dp ShadowElevation(const FloatingActionButtonElevation& e, const std::shared_ptr<foundation::MutableInteractionSource>& source) {
        auto& c = *rt::Composition::Current();
        auto state = c.Remember<std::shared_ptr<ElevationState>>([&] { return std::make_shared<ElevationState>(e); },
                                                               rt::Keys(std::hash<const void*>{}(source.get())));
        c.DisposableEffect(rt::Keys(std::hash<const void*>{}(state.get())), [state, source]() -> std::function<void()> {
            if (!source) return {};
            const auto token = source->Subscribe([state](const Interaction& interaction) { OnInteraction(*state, interaction); });
            return [state, source, token] { source->Unsubscribe(token); state->animation.Cancel(); };
        });
        c.LaunchedEffect(rt::Keys(std::hash<const void*>{}(state.get()), e.defaultElevation.value, e.pressedElevation.value,
                                  e.hoveredElevation.value, e.focusedElevation.value), [state, e] { return Update(state, e); });
        return ui::Dp(state->animatable.Float());
    }
}
