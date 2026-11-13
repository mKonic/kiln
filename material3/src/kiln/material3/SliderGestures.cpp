#include "kiln/material3/internal/Slider.h"
#include <kiln/Function.h>

#include <cmath>

namespace kiln::material3::internal {
    using namespace foundation;
    using namespace ui;
    namespace {
        struct Callbacks { kiln::Function<void(float)> single; kiln::Function<void(SliderRange)> range; kiln::Function<void()> finished; };
        struct InteractionGuard {
            std::shared_ptr<MutableInteractionSource> source;
            std::uint64_t press = 0, drag = 0;
            RangeSliderState* range = nullptr;
            std::weak_ptr<const char> alive;
            std::shared_ptr<Callbacks> callbacks;
            void EndPress(bool released) {
                if (!press) return;
                if (released) source->End(Interaction::Kind::Press, press); else source->Cancel(Interaction::Kind::Press, press);
                press = 0;
            }
            void EndDrag(bool released) {
                if (!drag) return;
                if (released) source->End(Interaction::Kind::Drag, drag); else source->Cancel(Interaction::Kind::Drag, drag);
                drag = 0;
                if (range && !alive.expired()) range->SetDragging(false);
                if (callbacks && callbacks->finished) callbacks->finished();
            }
            ~InteractionGuard() { EndPress(false); EndDrag(false); }
        };
        rt::Task<> SingleTap(PointerInputScope& s, SliderState& state, std::shared_ptr<MutableInteractionSource> source, std::shared_ptr<Callbacks> callbacks) {
            struct Reset {
                SliderState& state;
                std::weak_ptr<const char> alive;
                ~Reset() { if (!alive.expired()) state.SetDragging(false); }
            } reset{ state, state.AliveToken() };
            InteractionGuard guard{ source };
            TapCallbacks tap;
            tap.onPress = [&](Offset offset) {
                guard.press = source->Start(Interaction::Kind::Press, offset);
                state.OnPress(offset);
            };
            tap.onPressEnd = [&](bool released) { guard.EndPress(released); };
            tap.onTap = [&](Offset) {
                state.DispatchRawDelta(0, callbacks->single);
                if (!state.IsDragging() && callbacks->finished) callbacks->finished();
            };
            co_await DetectTapGestures(s, std::move(tap));
        }
        rt::Task<> RangeDrag(PointerInputScope& s, RangeSliderState& state, std::shared_ptr<MutableInteractionSource> start,
                             std::shared_ptr<MutableInteractionSource> end, std::shared_ptr<Callbacks> callbacks) {
            for (;;) {
                const auto down = *co_await AwaitFirstDown(s, false);
                state.UpdateMinMaxPx();
                const float x = state.IsRtl() ? float(state.TotalWidth()) - down.position.x : down.position.x;
                const float difference = std::abs(state.RawOffsetStart() - x) - std::abs(state.RawOffsetEnd() - x);
                bool draggingStart = difference != 0 ? difference < 0 : state.RawOffsetStart() > x;
                InteractionGuard guard{ draggingStart ? start : end, 0, 0, &state, state.AliveToken(), callbacks };
                guard.press = guard.source->Start(Interaction::Kind::Press, down.position);
                const float slop = PointerSlop(s.Config(), down.type);
                Offset over;
                std::optional<PointerInputChange> change;
                do {
                    const auto event = co_await s.AwaitPointerEvent();
                    if (event->changes.empty()) { change.reset(); break; }
                    change = event->changes.front();
                    over = over + change->PositionChange();
                } while (change->pressed && over.x * over.x + over.y * over.y < slop * slop);
                if (change && std::abs(over.x) > std::abs(over.y)) {
                    change->Consume();
                    guard.EndPress(false);
                    if (std::abs(state.RawOffsetEnd() - x) < slop && std::abs(state.RawOffsetStart() - x) < slop)
                        draggingStart = state.IsRtl() ? over.x >= 0 : over.x < 0;
                    guard.source = draggingStart ? start : end;
                    guard.drag = guard.source->Start(Interaction::Kind::Drag);
                    const float initial = x - (draggingStart ? state.RawOffsetStart() : state.RawOffsetEnd());
                    state.OnDrag(draggingStart, initial + (state.IsRtl() ? -over.x : over.x), callbacks->range);
                    state.SetDragging(true);
                    bool success = !change->pressed;
                    auto id = down.id;
                    while (change->pressed) {
                        auto event = co_await s.AwaitPointerEvent();
                        PointerInputChange* next = nullptr;
                        for (auto& c : event->changes) if (c.id == id) next = &c;
                        if (!next) break;
                        if (next->ChangedToUpIgnoreConsumed()) {
                            const PointerInputChange* replacement = nullptr;
                            for (const auto& c : event->changes) if (c.pressed) { replacement = &c; break; }
                            if (!replacement) { success = true; break; }
                            id = replacement->id;
                            continue;
                        }
                        if (next->IsConsumed()) break;
                        const float delta = next->PositionChange().x;
                        if (delta != 0) {
                            state.OnDrag(draggingStart, state.IsRtl() ? -delta : delta, callbacks->range);
                            next->Consume();
                        }
                    }
                    guard.EndDrag(success);
                } else if (change && !change->pressed) {
                    guard.EndPress(true);
                    state.OnDrag(draggingStart, x - (draggingStart ? state.RawOffsetStart() : state.RawOffsetEnd()), callbacks->range);
                    if (callbacks->finished) callbacks->finished();
                } else guard.EndPress(false);
                co_await AwaitAllPointersUp(s);
            }
        }
    }
    Modifier SliderGestures(Modifier m, SliderState& state, std::shared_ptr<MutableInteractionSource> source, bool enabled,
                            kiln::Function<void(float)> onChange, kiln::Function<void()> finished) {
        auto& c = *rt::Composition::Current();
        auto box = c.Remember<std::shared_ptr<Callbacks>>([] { return std::make_shared<Callbacks>(); });
        box->single = std::move(onChange); box->finished = std::move(finished);
        if (enabled) m = PointerInput(m, rt::Keys(std::uintptr_t(&state), std::uintptr_t(source.get())), [&state, source, box](PointerInputScope& s) { return SingleTap(s, state, source, box); });
        auto& drag = RememberDraggableState([&state, box](float delta) { state.DispatchRawDelta(delta, box->single); });
        DraggableOptions options;
        options.enabled = enabled; options.interactionSource = source;
        options.reverseDirection = state.Orientation() == Orientation::Vertical ? state.ReverseVerticalDirection() : state.IsRtl();
        options.startDragImmediately = state.IsDragging();
        options.onDragStarted = [&state](Offset) { state.SetDragging(true); };
        options.onDragStopped = [&state, box](float) { state.SetDragging(false); if (box->finished) box->finished(); };
        return Draggable(m, drag, state.Orientation(), options);
    }
    Modifier RangeSliderGestures(Modifier m, RangeSliderState& state, std::shared_ptr<MutableInteractionSource> start,
                                 std::shared_ptr<MutableInteractionSource> end, bool enabled,
                                 kiln::Function<void(SliderRange)> onChange, kiln::Function<void()> finished) {
        auto box = rt::Composition::Current()->Remember<std::shared_ptr<Callbacks>>([] { return std::make_shared<Callbacks>(); });
        box->range = std::move(onChange); box->finished = std::move(finished);
        if (!enabled) return m;
        return PointerInput(m, rt::Keys(std::uintptr_t(&state), std::uintptr_t(start.get()), std::uintptr_t(end.get())), [&state, start, end, box](PointerInputScope& s) { return RangeDrag(s, state, start, end, box); });
    }
}
