#include "kiln/material3/Label.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/MutatorMutex.h"
#include "kiln/runtime/Composition.h"

namespace kiln::material3 {
    using namespace foundation;

    namespace {
        class LabelScope final : public TooltipScope {
        public:
            std::optional<LayoutCoordinates> ObtainAnchorBounds() const override { return anchorBounds->Get(); }
            std::shared_ptr<const ui::PopupPositionProvider> ObtainPositionProvider() const override { return positionProvider; }
            std::shared_ptr<rt::State<std::optional<LayoutCoordinates>>> anchorBounds =
                std::make_shared<rt::State<std::optional<LayoutCoordinates>>>(std::nullopt);
            std::shared_ptr<const ui::PopupPositionProvider> positionProvider;
        };

        rt::Task<> ShowWhileActive(TooltipState state) { co_await state.Show(MutatePriority::UserInput); }

        // HandleInteractions: shown while a press, drag or hover is active, reacting only when
        // that changes (so a hover ending mid-drag leaves it shown).
        void HandleInteractions(bool enabled, const TooltipState& state, std::shared_ptr<MutableInteractionSource> source) {
            if (!enabled) return;
            auto& c = *rt::Composition::Current();
            // Its own scope: the interactions recompose this, not the caller.
            c.Child([&c, state, source] {
                const bool isVisible = source->IsPressed() || source->IsDragged() || source->IsHovered();
                c.LaunchedEffect(rt::Keys(source.get(), isVisible), [state, isVisible]() -> rt::Task<> {
                    if (isVisible) return ShowWhileActive(state);
                    state.Dismiss();
                    return [] () -> rt::Task<> { co_return; }();
                });
            });
        }
    }

    void Label(kiln::Function<void(const TooltipScope&)> label, const ui::Modifier& modifier, const LabelOptions& options,
               kiln::Function<void()> content) {
        auto& c = *rt::Composition::Current();
        auto source = options.interactionSource;
        if (!source) source = c.Remember<std::shared_ptr<MutableInteractionSource>>([] { return std::make_shared<MutableInteractionSource>(); });
        // Has the same positioning logic as PlainTooltips
        const auto positionProvider = TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition::Above);
        const TooltipState state = options.isPersistent
            ? c.Remember<TooltipState>([] { return internal::MakeLabelState(); })
            : internal::RememberBasicTooltipState(false, true, c.Remember<std::shared_ptr<MutatorMutex>>([] { return std::make_shared<MutatorMutex>(); }));
        auto& scope = c.Remember<std::shared_ptr<LabelScope>>([] { return std::make_shared<LabelScope>(); });
        scope->positionProvider = positionProvider;
        TooltipBoxOptions box;
        box.focusable = false;
        box.enableUserInput = false;
        const std::shared_ptr<LabelScope> s = scope;
        internal::BasicTooltipBox(positionProvider, [s, label] { label(*s); }, state, modifier, box, [s, content] {
            Box(OnGloballyPositioned({}, [s](const LayoutCoordinates& l) { s->anchorBounds->Set(l); }), Alignment::TopStart(), false, content);
        });
        HandleInteractions(!options.isPersistent, state, source);
    }

}
