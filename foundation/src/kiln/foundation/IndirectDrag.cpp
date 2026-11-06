#include "kiln/foundation/IndirectDrag.h"

#include <algorithm>
#include <cmath>

namespace kiln::foundation {

    using ui::IndirectPointerEvent;
    using ui::IndirectPointerInputChange;
    using Axis = ui::IndirectPointerEventPrimaryDirectionalMotionAxis;
    using ui::PointerEventPass;

    namespace { constexpr float kPixelSensibility = 2; }

    // primaryAxisPosition: the pad's primary axis drives our axis; without one, as is.
    Offset IndirectDragDetector::AxisPosition(Offset p, Axis axis) const {
        float v;
        if (axis == Axis::X) v = p.x;
        else if (axis == Axis::Y) v = p.y;
        else return p;
        return orientation == Orientation::Horizontal ? Offset{ v, 0 } : Offset{ 0, v };
    }

    Offset IndirectDragDetector::PositionChange(const IndirectPointerInputChange& c, Axis axis, bool ignoreConsumed) const {
        if (!ignoreConsumed && c.isConsumed) return {};
        return AxisPosition(c.position, axis) - AxisPosition(c.previousPosition, axis);
    }

    std::optional<Offset> IndirectDragDetector::PostSlop(Offset change) {
        m_SlopTotal = m_SlopTotal + change;
        const float main = Main(m_SlopTotal);
        const float inDirection = std::abs(main);
        if (!(inDirection > 0 && inDirection >= touchSlop)) return std::nullopt;
        const float post = main - (main > 0 ? touchSlop : -touchSlop);
        const float cross = orientation == Orientation::Horizontal ? m_SlopTotal.y : m_SlopTotal.x;
        return orientation == Orientation::Horizontal ? Offset{ post, cross } : Offset{ cross, post };
    }

    void IndirectDragDetector::ToAwaitDown() {
        m_State = State::AwaitDown;
        m_AwaitSlop = AwaitSlop::NotInitialized;
        m_ConsumedOnInitial = false;
    }

    void IndirectDragDetector::ToAwaitTouchSlop(const IndirectPointerInputChange& down, std::int64_t pointer, Offset initial) {
        m_State = State::AwaitTouchSlop;
        m_InitialDown = down;
        m_Pointer = pointer;
        m_SlopTotal = initial;
        m_VerifyInFinal = false;
    }

    void IndirectDragDetector::Reset() {
        ToAwaitDown();
        if (m_Listening) SendCancel();
        m_Velocity.Reset();
    }

    void IndirectDragDetector::SendStart(const IndirectPointerInputChange& down, const IndirectPointerInputChange& trigger,
                                         Axis axis, Offset overSlop) {
        m_Velocity.Reset();
        m_Velocity.AddPosition(down.uptimeMillis, AxisPosition(down.position, axis));
        m_Listening = true;
        if (callbacks.onStart) callbacks.onStart(AxisPosition(trigger.position, axis) - overSlop);
    }

    void IndirectDragDetector::SendDelta(const IndirectPointerInputChange& c, Axis axis, Offset amount) {
        if (std::abs(Main(amount)) <= kPixelSensibility) return;
        m_Velocity.AddPosition(c.uptimeMillis, AxisPosition(c.position, axis));
        if (callbacks.onDelta) callbacks.onDelta(amount);
    }

    void IndirectDragDetector::SendStop(const IndirectPointerInputChange& c, Axis axis) {
        m_Velocity.AddPosition(c.uptimeMillis, AxisPosition(c.position, axis));
        const Offset v = m_Velocity.CalculateVelocity({ maximumFlingVelocity, maximumFlingVelocity });
        m_Velocity.Reset();
        m_Listening = false;
        if (callbacks.onStop) callbacks.onStop(v);
    }

    void IndirectDragDetector::SendCancel() {
        m_Listening = false;
        if (callbacks.onCancel) callbacks.onCancel();
    }

    void IndirectDragDetector::Process(IndirectPointerEvent& e, PointerEventPass pass) {
        if (e.changes.empty()) return;
        const Axis axis = e.primaryDirectionalMotionAxis;
        auto findPointer = [&](std::int64_t id) -> IndirectPointerInputChange* {
            for (auto& c : e.changes) if (c.id == id) return &c;
            return nullptr;
        };
        auto firstPressed = [&]() -> IndirectPointerInputChange* {
            for (auto& c : e.changes) if (c.pressed) return &c;
            return nullptr;
        };
        switch (m_State) {
            case State::AwaitDown: {
                if (!std::all_of(e.changes.begin(), e.changes.end(), [](const auto& c) { return c.ChangedToDownIgnoreConsumed(); })) return;
                IndirectPointerInputChange& down = e.changes.front();
                if (m_AwaitSlop == AwaitSlop::NotInitialized)
                    m_AwaitSlop = callbacks.startDragImmediately && callbacks.startDragImmediately() ? AwaitSlop::No : AwaitSlop::Yes;
                if (pass == PointerEventPass::Initial && m_AwaitSlop == AwaitSlop::No) {
                    down.Consume();
                    m_ConsumedOnInitial = true;
                }
                if (pass == PointerEventPass::Main) {
                    if (m_AwaitSlop == AwaitSlop::Yes) {
                        ToAwaitTouchSlop(down, down.id);
                    } else if (m_ConsumedOnInitial) {
                        SendStart(down, down, axis, {});
                        SendDelta(down, axis, {});
                        m_State = State::Dragging;
                        m_Pointer = down.id;
                    }
                }
                return;
            }
            case State::AwaitTouchSlop: {
                if (pass == PointerEventPass::Initial) return;
                IndirectPointerInputChange* drag = findPointer(m_Pointer);
                if (!drag) {
                    drag = firstPressed();
                    if (!drag) { ToAwaitDown(); return; }
                    m_Pointer = drag->id;
                }
                if (pass == PointerEventPass::Main) {
                    if (!drag->isConsumed) {
                        if (drag->ChangedToUpIgnoreConsumed()) {
                            if (auto* other = firstPressed()) m_Pointer = other->id;
                            else ToAwaitDown();
                        } else if (auto post = PostSlop(PositionChange(*drag, axis, true))) {
                            drag->Consume();
                            SendStart(*m_InitialDown, *drag, axis, *post);
                            SendDelta(*drag, axis, *post);
                            m_State = State::Dragging;
                            m_Pointer = drag->id;
                        } else {
                            m_VerifyInFinal = true;
                        }
                    } else {
                        // Someone else took it: wait for a chance to pick it up again.
                        m_State = State::AwaitGesturePickup;
                        m_SlopTotal = {};
                    }
                }
                if (pass == PointerEventPass::Final && m_VerifyInFinal && m_State == State::AwaitTouchSlop) {
                    if (drag->isConsumed) { m_State = State::AwaitGesturePickup; m_SlopTotal = {}; }
                    else m_VerifyInFinal = false;
                }
                return;
            }
            case State::AwaitGesturePickup: {
                if (pass != PointerEventPass::Final) return;
                const bool unconsumed = std::all_of(e.changes.begin(), e.changes.end(), [](const auto& c) { return !c.isConsumed; });
                if (!firstPressed()) {
                    ToAwaitDown();
                } else if (unconsumed && m_InitialDown) {
                    const Offset initial = AxisPosition(e.changes.front().position, axis) - AxisPosition(m_InitialDown->position, axis);
                    ToAwaitTouchSlop(*m_InitialDown, m_Pointer, initial);
                }
                return;
            }
            case State::Dragging: {
                if (pass != PointerEventPass::Main) return;
                IndirectPointerInputChange* drag = findPointer(m_Pointer);
                if (!drag) return;
                if (drag->ChangedToUpIgnoreConsumed()) {
                    if (auto* other = firstPressed()) { m_Pointer = other->id; return; }
                    if (!drag->isConsumed) SendStop(*drag, axis);
                    else SendCancel();
                    ToAwaitDown();
                } else if (drag->isConsumed) {
                    SendCancel();
                } else {
                    // isDraggableZeroDeltaConsumptionEnabled: every move is ours.
                    SendDelta(*drag, axis, PositionChange(*drag, axis, false));
                    drag->Consume();
                }
                return;
            }
        }
    }

    namespace {
        struct IndirectDragParams {
            std::shared_ptr<IndirectDragDetector> detector;
            bool operator==(const IndirectDragParams& o) const { return detector == o.detector; }
        };
        struct IndirectDragNode final : ModifierNode, ui::IndirectPointerInputModifier {
            IndirectDragParams params;
            explicit IndirectDragNode(IndirectDragParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ui::IndirectPointerInputModifier* AsIndirectPointerInput() override { return this; }
            void OnDetach() override { params.detector->Reset(); }
            void OnIndirectPointerEvent(IndirectPointerEvent& e, PointerEventPass pass) override {
                if (ui::Owner* o = Owner() ? Owner()->GetOwner() : nullptr) {
                    params.detector->touchSlop = PointerSlop(o->Config(), ui::PointerType::Touch);
                    params.detector->maximumFlingVelocity = o->Config().maximumFlingVelocity;
                }
                params.detector->Process(e, pass);
            }
            void OnCancelIndirectPointerInput() override { params.detector->Reset(); }
        };
    }

    Modifier IndirectDrag(Modifier m, std::shared_ptr<IndirectDragDetector> detector) {
        return m.With<IndirectDragNode>(IndirectDragParams{ std::move(detector) });
    }

}
