#include "kiln/material3/SwipeToDismissBox.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/material3/internal/DraggableAnchors.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Locals.h"

#include <cmath>

namespace kiln::material3 {

    using namespace foundation;

    SwipeToDismissBoxState::SwipeToDismissBoxState(SwipeToDismissBoxValue initialValue, std::function<float(float)> positionalThreshold,
                                                   std::function<bool(SwipeToDismissBoxValue)> confirm)
        : m_PositionalThreshold(std::move(positionalThreshold)),
          m_Anchored(initialValue, [confirm](const SwipeToDismissBoxValue& v) { return !confirm || confirm(v); }) {}

    float SwipeToDismissBoxState::Progress() const {
        const auto& a = m_Anchored.Anchors();
        const auto closest = a.ClosestAnchor(m_Anchored.Offset());
        return m_Anchored.Progress(m_Anchored.CurrentValue(), closest.value_or(m_Anchored.CurrentValue()));
    }

    SwipeToDismissBoxValue SwipeToDismissBoxState::DismissDirection() const {
        const float offset = m_Anchored.Offset();
        if (offset == 0 || std::isnan(offset)) return SwipeToDismissBoxValue::Settled;
        return offset > 0 ? SwipeToDismissBoxValue::StartToEnd : SwipeToDismissBoxValue::EndToStart;
    }

    rt::Task<> SwipeToDismissBoxState::SnapTo(SwipeToDismissBoxValue target) { co_await m_Anchored.SnapTo(target); }
    rt::Task<> SwipeToDismissBoxState::Reset() { co_await m_Anchored.AnimateTo(SwipeToDismissBoxValue::Settled); }
    rt::Task<> SwipeToDismissBoxState::Dismiss(SwipeToDismissBoxValue direction) { co_await m_Anchored.AnimateTo(direction); }

    namespace SwipeToDismissBoxDefaults {
        std::function<float(float)> positionalThreshold() {
            const ui::Density d = rt::Composition::Current()->Read(ui::LocalDensity);
            return [px = d.ToPx(ui::Dp(56))](float) { return px; };
        }
    }

    std::shared_ptr<SwipeToDismissBoxState> RememberSwipeToDismissBoxState(SwipeToDismissBoxValue initialValue, std::function<float(float)> threshold,
                                                                           std::function<bool(SwipeToDismissBoxValue)> confirm, rt::Key key) {
        if (!threshold) threshold = SwipeToDismissBoxDefaults::positionalThreshold();
        // SwipeToDismissBoxState.Saver keeps the current value.
        rt::Saver<std::shared_ptr<SwipeToDismissBoxState>> saver{
            [](const std::shared_ptr<SwipeToDismissBoxState>& s) { return rt::SavedValue(double(int(s->CurrentValue()))); },
            [threshold, confirm](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<SwipeToDismissBoxState>> {
                if (auto n = rt::SavedNumber(saved)) return std::make_shared<SwipeToDismissBoxState>(SwipeToDismissBoxValue(int(*n)), threshold, confirm);
                return std::nullopt;
            }
        };
        return rt::RememberSaveable<std::shared_ptr<SwipeToDismissBoxState>>(
            [&] { return std::make_shared<SwipeToDismissBoxState>(initialValue, threshold, confirm); }, saver, rt::Keys(), key);
    }

    void SwipeToDismissBox(std::shared_ptr<SwipeToDismissBoxState> state, std::function<void()> backgroundContent, const ui::Modifier& modifier,
                           const SwipeToDismissBoxOptions& o, std::function<void()> content) {
        auto& c = *rt::Composition::Current();
        AnchoredDraggableOptions drag;
        drag.enabled = o.gesturesEnabled && state->SettledValue() == SwipeToDismissBoxValue::Settled;
        // AnchoredDraggableDefaults.flingBehavior with the state's positional threshold.
        drag.positionalThreshold = [s = state.get()](float distance) { return s->PositionalThreshold() ? s->PositionalThreshold()(distance) : distance / 2; };
        const bool toEnd = o.enableDismissFromStartToEnd, toStart = o.enableDismissFromEndToStart;
        const auto anchors = c.Remember<std::shared_ptr<const internal::AnchorsCalculation<SwipeToDismissBoxValue>>>(
            [&] {
                return std::make_shared<const internal::AnchorsCalculation<SwipeToDismissBoxValue>>([state, toEnd, toStart](ui::IntSize size, const ui::Constraints&) {
                    const float width = float(size.width);
                    foundation::DraggableAnchors<SwipeToDismissBoxValue> a;
                    a.At(SwipeToDismissBoxValue::Settled, 0);
                    if (toEnd) a.At(SwipeToDismissBoxValue::StartToEnd, width);
                    if (toStart) a.At(SwipeToDismissBoxValue::EndToStart, -width);
                    const bool initialized = state->Anchored().Anchors().Size() > 0;
                    const SwipeToDismissBoxValue previous = state->CurrentValue(), target = state->TargetValue();
                    const SwipeToDismissBoxValue newTarget = !initialized && a.HasPositionFor(previous) ? previous
                                                           : a.HasPositionFor(target)                   ? target
                                                                                                        : SwipeToDismissBoxValue::Settled;
                    return std::pair{ std::move(a), newTarget };
                });
            },
            rt::Keys(reinterpret_cast<std::uintptr_t>(state.get()), toEnd, toStart));
        Box(AnchoredDraggable(modifier, state->Anchored(), Orientation::Horizontal, drag), ui::Alignment::TopStart(), true, [=] {
            Row(MatchParentSize({}), std::move(backgroundContent));
            Row(internal::DraggableAnchors({}, state->Anchored(), Orientation::Horizontal, anchors), std::move(content));
        });
        // Only once settled in a dismissed direction.
        const SwipeToDismissBoxValue settled = state->SettledValue();
        const auto onDismiss = o.onDismiss;
        c.LaunchedEffect(rt::Keys(int(settled)), [settled, state, onDismiss]() -> rt::Task<> {
            if (settled != SwipeToDismissBoxValue::Settled && onDismiss) onDismiss(state->DismissDirection());
            co_return;
        });
    }

}
