#include "kiln/foundation/Gestures.h"

#include "kiln/runtime/Task.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Semantics.h"

#include <cmath>

namespace kiln::foundation {

    namespace {

        struct PointerParams {
            rt::Keys keys;
            std::shared_ptr<PointerInputHandler> handler;
            // Equal keys keep the running coroutine; the handler object is refreshed regardless.
            bool operator==(const PointerParams& o) const { return keys == o.keys && handler == o.handler; }
        };

        class PointerInputNode final : public ModifierNode, public PointerInputModifier, public PointerInputScope {
        public:
            PointerParams params;
            explicit PointerInputNode(PointerParams p) : params(std::move(p)) {}
            void OnParamsChanged() {
                if (!(params.keys == m_StartedKeys)) Restart(false);
            }
            PointerInputModifier* AsPointerInput() override { return this; }
            void OnDetach() override { Stop(); }

            void OnPointerEvent(PointerEvent& ev, PointerEventPass pass, IntSize bounds) override {
                m_Size = bounds;
                if (!m_Job.Active()) Restart(true);
                if (!m_Waiter || m_WaitPass != pass) return;
                auto h = m_Waiter;
                m_Waiter = nullptr;
                if (m_Job.Active()) m_JobState->unregister = nullptr;
                // The copy shares the consumption flags with the dispatch, so Consume() on it is
                // seen by later passes and nodes.
                m_Event = ev;
                m_Current = ev;
                h.resume();
            }
            void OnCancelPointerInput() override { Restart(false); }

            const ViewConfiguration& Config() const override {
                static const ViewConfiguration fallback;
                return Owner() && Owner()->GetOwner() ? Owner()->GetOwner()->Config() : fallback;
            }
            Density GetDensity() const override {
                return Owner() && Owner()->GetOwner() ? Owner()->GetOwner()->GetDensity() : Density{};
            }
            bool InScrollableContainer() const override {
                for (const LayoutNode* n = Owner(); n; n = n->Parent())
                    for (const ModifierNode* m : n->ModifierNodes())
                        if (m->IsScrollableContainer()) return true;
                return false;
            }

        protected:
            void Suspend(std::coroutine_handle<> h, PointerEventPass pass, std::int64_t timeout,
                         const std::shared_ptr<rt::JobState>& job) override {
                m_Waiter = h;
                m_WaitPass = pass;
                m_JobState = job;
                m_TimedOut = false;
                const std::uint64_t ticket = ++m_Ticket;
                if (timeout >= 0) {
                    // A timer resumes with nullopt unless an event came first (the ticket moved on).
                    m_TimerJob = rt::Launch(*job->dispatcher, Timer(this, ticket, timeout));
                }
                job->unregister = [this] { m_Waiter = nullptr; m_TimerJob.Cancel(); };
            }
            std::optional<PointerEvent> TakeEvent() override {
                m_TimerJob.Cancel();
                if (m_TimedOut) { m_TimedOut = false; return std::nullopt; }
                auto e = std::move(m_Event);
                m_Event.reset();
                return e;
            }

        private:
            static rt::Task<> Timer(PointerInputNode* self, std::uint64_t ticket, std::int64_t ms) {
                co_await rt::Delay(ms);
                if (self->m_Ticket != ticket || !self->m_Waiter) co_return;
                auto h = self->m_Waiter;
                self->m_Waiter = nullptr;
                self->m_TimedOut = true;
                h.resume();
            }

            void Stop() {
                m_Waiter = nullptr;
                m_TimerJob.Cancel();
                m_Job.Cancel();
            }
            void Restart(bool start) {
                Stop();
                if (!start || !params.handler) return;
                m_StartedKeys = params.keys;
                m_Job = rt::Launch(rt::Dispatcher::Main(), (*params.handler)(*this));
            }

            rt::Job m_Job, m_TimerJob;
            std::shared_ptr<rt::JobState> m_JobState;
            rt::Keys m_StartedKeys;
            std::coroutine_handle<> m_Waiter;
            PointerEventPass m_WaitPass = PointerEventPass::Main;
            std::optional<PointerEvent> m_Event;
            bool m_TimedOut = false;
            std::uint64_t m_Ticket = 0;
        };
    }

    Modifier PointerInput(Modifier m, rt::Keys keys, PointerInputHandler handler) {
        // The handler is stored behind a shared_ptr so the element compares by keys only; a new
        // lambda with the same keys keeps the running gesture (pointerInput(key) semantics).
        auto h = std::make_shared<PointerInputHandler>(std::move(handler));
        return m.With<PointerInputNode>(PointerParams{ std::move(keys), std::move(h) });
    }

    // --- TapGestureDetector.kt:125-260, 386-457 -----------------------------------------------

    namespace {
        bool AllUp(const PointerEvent& e) {
            for (const auto& c : e.changes) if (c.pressed) return false;
            return true;
        }
        rt::Task<std::optional<PointerInputChange>> AwaitFirstDown(PointerInputScope& s, bool requireUnconsumed = true) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                bool allDown = !e->changes.empty();
                for (const auto& c : e->changes)
                    if (!(requireUnconsumed ? c.ChangedToDown() : c.ChangedToDownIgnoreConsumed())) allDown = false;
                if (allDown) co_return e->changes[0];
            }
        }
        // waitForUpOrCancellation: null when consumed by someone else or out of bounds.
        rt::Task<std::optional<PointerInputChange>> WaitForUpOrCancellation(PointerInputScope& s) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                bool allUp = true;
                for (const auto& c : e->changes) if (!c.ChangedToUp()) allUp = false;
                if (allUp) co_return e->changes[0];
                for (const auto& c : e->changes)
                    if (c.IsConsumed() || c.IsOutOfBounds(s.Size())) co_return std::nullopt;
                auto check = co_await s.AwaitPointerEvent(PointerEventPass::Final);
                for (const auto& c : check->changes) if (c.IsConsumed()) co_return std::nullopt;
            }
        }
        enum class LongPress { Success, Released, Canceled };
        struct LongPressResult { LongPress kind; std::optional<PointerInputChange> up; };
        rt::Task<LongPressResult> WaitForLongPress(PointerInputScope& s, std::int64_t startUptime) {
            const std::int64_t timeout = s.Config().longPressTimeoutMillis;
            std::int64_t elapsedBase = startUptime;
            for (;;) {
                auto e = co_await s.AwaitPointerEventWithTimeout(timeout - 0, PointerEventPass::Main);
                if (!e) co_return { LongPress::Success, {} };
                if (!e->changes.empty() && e->changes[0].uptimeMillis - elapsedBase >= timeout) co_return { LongPress::Success, {} };
                if (AllUp(*e)) co_return { LongPress::Released, e->changes[0] };
                if (e->classification == 2) co_return { LongPress::Success, {} };   // deep press
                for (const auto& c : e->changes)
                    if (c.IsConsumed() || c.IsOutOfBounds(s.Size())) co_return { LongPress::Canceled, {} };
                auto check = co_await s.AwaitPointerEventWithTimeout(timeout, PointerEventPass::Final);
                if (!check) co_return { LongPress::Success, {} };
                for (const auto& c : check->changes) if (c.IsConsumed()) co_return { LongPress::Canceled, {} };
            }
        }
        rt::Task<> ConsumeUntilUp(PointerInputScope& s) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                for (auto& c : e->changes) c.Consume();
                bool any = false;
                for (const auto& c : e->changes) if (c.pressed) any = true;
                if (!any) co_return;
            }
        }
        rt::Task<std::optional<PointerInputChange>> AwaitSecondDown(PointerInputScope& s, const PointerInputChange& firstUp) {
            const std::int64_t minUptime = firstUp.uptimeMillis + s.Config().doubleTapMinTimeMillis;
            const std::int64_t deadline = firstUp.uptimeMillis + s.Config().doubleTapTimeoutMillis;
            for (;;) {
                // Timeouts are relative to the last event; recompute from the deadline.
                auto e = co_await s.AwaitPointerEventWithTimeout(s.Config().doubleTapTimeoutMillis, PointerEventPass::Main);
                if (!e) co_return std::nullopt;
                if (!e->changes.empty() && e->changes[0].uptimeMillis > deadline) co_return std::nullopt;
                bool allDown = !e->changes.empty();
                for (const auto& c : e->changes) if (!c.ChangedToDown()) allDown = false;
                if (allDown && e->changes[0].uptimeMillis >= minUptime) co_return e->changes[0];
            }
        }
        rt::Task<> AwaitAllUp(PointerInputScope& s) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Final);
                if (AllUp(*e)) co_return;
            }
        }
    }

    rt::Task<> DetectTapGestures(PointerInputScope& s, TapCallbacks cb) {
        for (;;) {   // awaitEachGesture
            auto downOpt = co_await AwaitFirstDown(s);
            PointerInputChange down = *downOpt;
            down.Consume();
            if (cb.onPress) cb.onPress(down.position);
            std::optional<PointerInputChange> upOrCancel;
            bool gestureDone = false;
            if (!cb.onLongPress) {
                upOrCancel = co_await WaitForUpOrCancellation(s);
            } else {
                LongPressResult r = co_await WaitForLongPress(s, down.uptimeMillis);
                if (r.kind == LongPress::Success) {
                    cb.onLongPress(down.position);
                    co_await ConsumeUntilUp(s);
                    if (cb.onPressEnd) cb.onPressEnd(true);
                    gestureDone = true;
                } else if (r.kind == LongPress::Released) {
                    upOrCancel = r.up;
                }
            }
            if (!gestureDone) {
                if (!upOrCancel) {
                    if (cb.onPressEnd) cb.onPressEnd(false);
                } else {
                    upOrCancel->Consume();
                    if (cb.onPressEnd) cb.onPressEnd(true);
                    if (!cb.onDoubleTap) {
                        if (cb.onTap) cb.onTap(upOrCancel->position);
                    } else {
                        auto second = co_await AwaitSecondDown(s, *upOrCancel);
                        if (!second) {
                            if (cb.onTap) cb.onTap(upOrCancel->position);
                        } else {
                            if (cb.onPress) cb.onPress(second->position);
                            auto secondUp = co_await WaitForUpOrCancellation(s);
                            if (secondUp) {
                                secondUp->Consume();
                                if (cb.onPressEnd) cb.onPressEnd(true);
                                cb.onDoubleTap(secondUp->position);
                            } else {
                                if (cb.onPressEnd) cb.onPressEnd(false);
                                if (cb.onTap) cb.onTap(upOrCancel->position);
                            }
                        }
                    }
                }
            }
            co_await AwaitAllUp(s);
        }
    }

    // --- drag ---------------------------------------------------------------------------------

    float PointerSlop(const ViewConfiguration& c, PointerType type) {
        return type == PointerType::Mouse ? c.touchSlop * (0.125f / 18.0f) : c.touchSlop;
    }

    namespace {
        const PointerInputChange* Find(const PointerEvent& e, std::int64_t id) {
            for (const auto& c : e.changes) if (c.id == id) return &c;
            return nullptr;
        }
        PointerInputChange* Find(PointerEvent& e, std::int64_t id) {
            for (auto& c : e.changes) if (c.id == id) return &c;
            return nullptr;
        }
        const PointerInputChange* FirstPressed(const PointerEvent& e) {
            for (const auto& c : e.changes) if (c.pressed) return &c;
            return nullptr;
        }
        bool IsPointerUp(const PointerEvent& e, std::int64_t id) {
            const auto* c = Find(e, id);
            return !c || !c->pressed;
        }
        float Length(Offset o) { return std::sqrt(o.x * o.x + o.y * o.y); }

        struct TouchSlopDetector {
            std::optional<Orientation> orientation;
            Offset total;
            float Main(Offset o) const { return orientation == Orientation::Horizontal ? o.x : o.y; }
            float Cross(Offset o) const { return orientation == Orientation::Horizontal ? o.y : o.x; }
            std::optional<Offset> PostSlop(Offset change, float slop) {
                total = total + change;
                const float inDir = orientation ? std::abs(Main(total)) : Length(total);
                if (!(inDir > 0 && inDir >= slop)) return std::nullopt;
                if (!orientation) {
                    const float len = Length(total);
                    return Offset{ total.x - total.x / len * slop, total.y - total.y / len * slop };
                }
                const float m = Main(total) - (Main(total) > 0 ? 1 : -1) * slop;
                const float c = Cross(total);
                return *orientation == Orientation::Horizontal ? Offset{ m, c } : Offset{ c, m };
            }
        };

        // awaitPointerSlopOrCancellation: returns the change that crossed slop, consumed by the
        // callback, or nullopt if cancelled.
        rt::Task<std::optional<PointerInputChange>> AwaitSlop(PointerInputScope& s, std::int64_t id, PointerType type,
                                                              std::optional<Orientation> o, Offset initial,
                                                              std::function<void(PointerInputChange&, Offset)> onSlop) {
            if (IsPointerUp(s.CurrentEvent(), id)) co_return std::nullopt;
            const float slop = PointerSlop(s.Config(), type);
            std::int64_t pointer = id;
            TouchSlopDetector det{ o, initial };
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                PointerInputChange* drag = Find(*e, pointer);
                if (!drag) co_return std::nullopt;
                if (drag->IsConsumed()) co_return std::nullopt;
                if (drag->ChangedToUpIgnoreConsumed()) {
                    const auto* other = FirstPressed(*e);
                    if (!other) co_return std::nullopt;
                    pointer = other->id;
                } else if (auto over = det.PostSlop(drag->PositionChangeIgnoreConsumed(), slop)) {
                    onSlop(*drag, *over);
                    if (drag->IsConsumed()) co_return *drag;
                    det.total = {};
                } else {
                    co_await s.AwaitPointerEvent(PointerEventPass::Final);
                    if (drag->IsConsumed()) co_return std::nullopt;
                }
            }
        }

        // drag(): follows the pointer until it lifts (returns the up) or motion is consumed (nullopt).
        rt::Task<std::optional<PointerInputChange>> Drag(PointerInputScope& s, std::int64_t id,
                                                         std::function<void(PointerInputChange&)> onDrag) {
            if (IsPointerUp(s.CurrentEvent(), id)) co_return std::nullopt;
            std::int64_t pointer = id;
            for (;;) {
                // awaitDragOrUp
                std::optional<PointerInputChange> change;
                for (;;) {
                    auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                    PointerInputChange* c = Find(*e, pointer);
                    if (!c) co_return std::nullopt;
                    if (c->ChangedToUpIgnoreConsumed()) {
                        const auto* other = FirstPressed(*e);
                        if (!other) { change = *c; break; }
                        pointer = other->id;
                    } else if (Length(c->PositionChangeIgnoreConsumed()) != 0) {
                        change = *c;
                        break;
                    }
                }
                if (change->IsConsumed()) co_return std::nullopt;
                if (change->ChangedToUpIgnoreConsumed()) co_return change;
                onDrag(*change);
                pointer = change->id;
            }
        }

        rt::Task<std::optional<PointerInputChange>> AwaitFirstDownPass(PointerInputScope& s, bool requireUnconsumed, PointerEventPass pass) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(pass);
                bool all = !e->changes.empty();
                for (const auto& c : e->changes)
                    if (!(requireUnconsumed ? c.ChangedToDown() : c.ChangedToDownIgnoreConsumed())) all = false;
                if (all) co_return e->changes[0];
            }
        }
    }

    rt::Task<> DetectDragGestures(PointerInputScope& s, std::optional<Orientation> lock, DragCallbacks cb) {
        for (;;) {
            auto initialDown = co_await AwaitFirstDownPass(s, false, PointerEventPass::Initial);
            auto down = co_await AwaitFirstDownPass(s, false, PointerEventPass::Main);
            std::optional<PointerInputChange> drag;
            Offset overSlop;
            do {
                drag = co_await AwaitSlop(s, down->id, down->type, lock, {}, [&](PointerInputChange& c, Offset over) {
                    c.Consume();
                    overSlop = over;
                });
            } while (drag && !drag->IsConsumed());
            // Gesture pickup: someone else consumed the slop crossing; wait for them to let go.
            while (!drag && FirstPressed(s.CurrentEvent())) {
                PointerEvent e;
                do {
                    e = *co_await s.AwaitPointerEvent(PointerEventPass::Final);
                    bool anyConsumed = false;
                    for (const auto& c : e.changes) if (c.IsConsumed()) anyConsumed = true;
                    if (!(anyConsumed && FirstPressed(e))) break;
                } while (true);
                if (FirstPressed(e)) {
                    const Offset initial = (e.changes.empty() ? down->position : e.changes[0].position) - down->position;
                    drag = co_await AwaitSlop(s, down->id, down->type, lock, initial, [&](PointerInputChange& c, Offset) {
                        c.Consume();
                        overSlop = c.PositionChange();
                    });
                }
            }
            (void)initialDown;
            if (drag) {
                if (cb.onDragStart) cb.onDragStart(*down, *drag, overSlop);
                if (cb.onDrag) cb.onDrag(*drag, overSlop);
                auto up = co_await Drag(s, drag->id, [&](PointerInputChange& c) {
                    if (cb.onDrag) cb.onDrag(c, c.PositionChange());
                    c.Consume();
                });
                if (!up) { if (cb.onDragCancel) cb.onDragCancel(); }
                else if (cb.onDragEnd) cb.onDragEnd(*up);
            }
            // awaitEachGesture: wait until every pointer is up before the next gesture.
            while (FirstPressed(s.CurrentEvent())) co_await s.AwaitPointerEvent(PointerEventPass::Final);
        }
    }

    namespace {
        // The shared clickable core: callbacks behind boxes refreshed every composition
        // (rememberUpdatedState), interactions, indication, keys, focus, semantics, taps.
        struct ClickState {
            std::function<void()> onClick;
            std::function<void(bool)> onPressed;
            std::function<void(SemanticsConfiguration&)> extraSemantics;
            std::function<void()> onLongClick, onDoubleClick;
            std::shared_ptr<MutableInteractionSource> source;
            std::uint64_t press = 0, keyPress = 0, hover = 0;
            rt::Job delayJob;
        };

        rt::Task<> HoverLoop(PointerInputScope& s, std::shared_ptr<ClickState> st) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                if (!e || !st->source) continue;
                if (e->type == PointerEventType::Enter && !st->hover) st->hover = st->source->Start(Interaction::Kind::Hover);
                else if (e->type == PointerEventType::Exit && st->hover) { st->source->End(Interaction::Kind::Hover, st->hover); st->hover = 0; }
            }
        }

        rt::Task<> DelayedPress(std::shared_ptr<ClickState> st, Offset at, std::int64_t delayMs) {
            co_await rt::Delay(delayMs);
            if (st->source && !st->press) st->press = st->source->Start(Interaction::Kind::Press, at);
        }

        Modifier ClickableCore(Modifier m, std::function<void()> onClick, std::function<void(bool)> onPressed,
                               std::function<void(SemanticsConfiguration&)> extra, ClickableOptions o) {
            auto st = std::make_shared<ClickState>();
            if (auto* c = rt::Composition::Current()) st = c->Remember<std::shared_ptr<ClickState>>([&] { return st; });
            st->onClick = std::move(onClick);
            st->onPressed = std::move(onPressed);
            st->extraSemantics = std::move(extra);
            st->onLongClick = o.onLongClick;
            st->onDoubleClick = o.onDoubleClick;
            const bool hasLong = bool(o.onLongClick), hasDouble = bool(o.onDoubleClick);
            const std::optional<std::string> longLabel = o.onLongClickLabel;
            // An indication needs a source; create one when the caller passed none.
            IndicationPtr indication = o.indication;
            if (!indication && o.useLocalIndication) {
                if (auto* c = rt::Composition::Current()) indication = c->Read(LocalIndication);
                if (!indication) indication = DefaultDebugIndication();
            }
            if (o.interactionSource) st->source = o.interactionSource;
            else if (indication && !st->source) st->source = std::make_shared<MutableInteractionSource>();
            const std::optional<Role> role = o.role;
            const std::optional<std::string> label = o.onClickLabel;
            const bool enabled = o.enabled;

            // Semantics come first so they hold even when disabled (applySemantics: role, onClick
            // with label, focusable or disabled).
            m = Semantics(std::move(m), [st, role, label, enabled, hasLong, longLabel](SemanticsConfiguration& c) {
                if (role) c.role = *role;
                c.onClick = [st] { if (st->onClick) st->onClick(); return true; };
                if (label) c.onClickLabel = *label;
                if (hasLong) {
                    c.onLongClick = [st] { if (st->onLongClick) st->onLongClick(); return true; };
                    if (longLabel) c.onLongClickLabel = *longLabel;
                }
                if (!enabled) c.disabled = true;
                if (st->extraSemantics) st->extraSemantics(c);
            }, true);
            if (indication && st->source) m = indication->Create(std::move(m), st->source);
            if (!enabled) return m;

            m = OnKeyEvent(std::move(m), [st](const KeyEvent& e) {
                const bool activate = e.keyCode == keys::Enter || e.keyCode == keys::NumpadEnter ||
                                      e.keyCode == keys::DpadCenter || e.keyCode == keys::Space;
                if (!activate) return false;
                if (e.type == KeyEventType::KeyDown) {
                    if (e.repeatCount == 0) {
                        if (st->onPressed) st->onPressed(true);
                        if (st->source && !st->keyPress) st->keyPress = st->source->Start(Interaction::Kind::Press);
                    }
                    return true;
                }
                if (e.type == KeyEventType::KeyUp) {
                    if (st->onPressed) st->onPressed(false);
                    if (st->source && st->keyPress) { st->source->End(Interaction::Kind::Press, st->keyPress); st->keyPress = 0; }
                    if (st->onClick) st->onClick();
                    return true;
                }
                return false;
            });
            m = Focusable(std::move(m), Focusability::SystemDefined, {}, st->source);
            // Hover: Enter / Exit events become hover interactions (AbstractClickableNode).
            if (st->source) m = PointerInput(std::move(m), rt::Keys(1), [st](PointerInputScope& s) { return HoverLoop(s, st); });
            return PointerInput(std::move(m), rt::Keys(0, hasLong, hasDouble), [st, hasLong, hasDouble](PointerInputScope& s) {
                TapCallbacks cb;
                if (hasLong) cb.onLongPress = [st](Offset) { if (st->onLongClick) st->onLongClick(); };
                if (hasDouble) cb.onDoubleTap = [st](Offset) { if (st->onDoubleClick) st->onDoubleClick(); };
                const bool delayed = s.InScrollableContainer();
                const std::int64_t delayMs = s.Config().tapTimeoutMillis;
                auto* dispatcher = &rt::Dispatcher::Main();
                cb.onPress = [st, delayed, delayMs, dispatcher](Offset at) {
                    if (st->onPressed) st->onPressed(true);
                    if (!st->source) return;
                    if (delayed) st->delayJob = rt::Launch(*dispatcher, DelayedPress(st, at, delayMs));
                    else st->press = st->source->Start(Interaction::Kind::Press, at);
                };
                cb.onPressEnd = [st](bool released) {
                    if (st->onPressed) st->onPressed(false);
                    // A tap released before the delay still shows a brief press (Clickable.kt).
                    if (st->source && !st->press && st->delayJob.Active() && released) {
                        st->delayJob.Cancel();
                        st->press = st->source->Start(Interaction::Kind::Press);
                    }
                    st->delayJob.Cancel();
                    if (st->source && st->press) {
                        if (released) st->source->End(Interaction::Kind::Press, st->press);
                        else st->source->Cancel(Interaction::Kind::Press, st->press);
                    }
                    st->press = 0;
                };
                cb.onTap = [st](Offset) { if (st->onClick) st->onClick(); };
                return DetectTapGestures(s, cb);
            });
        }
    }

    Modifier Clickable(Modifier m, std::function<void()> onClick, ClickableOptions o) {
        return ClickableCore(std::move(m), std::move(onClick), {}, {}, std::move(o));
    }

    Modifier Clickable(Modifier m, std::function<void()> onClick, std::function<void(bool)> onPressedChange, bool enabled) {
        ClickableOptions o;
        o.enabled = enabled;
        return ClickableCore(std::move(m), std::move(onClick), std::move(onPressedChange), {}, o);
    }

    Modifier Toggleable(Modifier m, bool value, std::function<void(bool)> onValueChange, ClickableOptions o) {
        return ClickableCore(std::move(m), [value, onValueChange] { if (onValueChange) onValueChange(!value); }, {},
                             [value](SemanticsConfiguration& c) { c.toggleableState = value ? 1 : 0; }, std::move(o));
    }

    Modifier TriStateToggleable(Modifier m, int state, std::function<void()> onClick, ClickableOptions o) {
        return ClickableCore(std::move(m), std::move(onClick), {}, [state](SemanticsConfiguration& c) { c.toggleableState = state; }, std::move(o));
    }

    Modifier Selectable(Modifier m, bool selected, std::function<void()> onClick, ClickableOptions o) {
        return ClickableCore(std::move(m), std::move(onClick), {}, [selected](SemanticsConfiguration& c) { c.selected = selected; }, std::move(o));
    }

    Modifier SelectableGroup(Modifier m) {
        return Semantics(std::move(m), [](SemanticsConfiguration& c) { c.isTraversalGroup = true; c.selectableGroup = true; });
    }

    // --- TransformGestureDetector.kt ---------------------------------------------------------

    namespace {
        bool Both(const PointerInputChange& c) { return c.pressed && c.previousPressed; }
    }

    Offset CalculateCentroid(const PointerEvent& e, bool useCurrent) {
        Offset sum{};
        int n = 0;
        for (const auto& c : e.changes)
            if (Both(c)) { sum = sum + (useCurrent ? c.position : c.previousPosition); ++n; }
        if (n == 0) return { std::nanf(""), std::nanf("") };
        return { sum.x / float(n), sum.y / float(n) };
    }

    float CalculateCentroidSize(const PointerEvent& e, bool useCurrent) {
        const Offset centroid = CalculateCentroid(e, useCurrent);
        if (std::isnan(centroid.x)) return 0;
        float d = 0;
        int n = 0;
        for (const auto& c : e.changes)
            if (Both(c)) { const Offset p = (useCurrent ? c.position : c.previousPosition) - centroid; d += std::hypot(p.x, p.y); ++n; }
        return d / float(n);
    }

    Offset CalculatePan(const PointerEvent& e) {
        const Offset cur = CalculateCentroid(e, true);
        if (std::isnan(cur.x)) return {};
        return cur - CalculateCentroid(e, false);
    }

    float CalculateZoom(const PointerEvent& e) {
        const float cur = CalculateCentroidSize(e, true), prev = CalculateCentroidSize(e, false);
        if (cur == 0 || prev == 0) return 1;
        return cur / prev;
    }

    float CalculateRotation(const PointerEvent& e) {
        int count = 0;
        for (const auto& c : e.changes) if (Both(c)) ++count;
        if (count < 2) return 0;
        const Offset curC = CalculateCentroid(e, true), prevC = CalculateCentroid(e, false);
        auto angle = [](Offset o) { return (o.x == 0 && o.y == 0) ? 0.0f : -std::atan2(o.x, o.y) * 180.0f / 3.14159265358979f; };
        float rotation = 0, weight = 0;
        for (const auto& c : e.changes) {
            if (!Both(c)) continue;
            const Offset prevO = c.previousPosition - prevC, curO = c.position - curC;
            float diff = angle(curO) - angle(prevO);
            if (diff > 180) diff -= 360; else if (diff < -180) diff += 360;
            const Offset sum = curO + prevO;
            const float w = std::hypot(sum.x, sum.y) / 2;
            rotation += diff * w;
            weight += w;
        }
        return weight == 0 ? 0 : rotation / weight;
    }

    rt::Task<> DetectTransformGestures(PointerInputScope& s, bool panZoomLock, std::function<void(Offset, Offset, float, float)> onGesture) {
        for (;;) {
            // awaitFirstDown(requireUnconsumed = false)
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                bool down = false;
                for (const auto& c : e->changes) if (c.ChangedToDownIgnoreConsumed()) down = true;
                if (down) break;
            }
            float rotation = 0, zoom = 1;
            Offset pan{};
            bool pastSlop = false, locked = false;
            const float slop = s.Config().touchSlop;
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                bool canceled = false, anyPressed = false;
                for (const auto& c : e->changes) { canceled = canceled || c.IsConsumed(); anyPressed = anyPressed || c.pressed; }
                if (!canceled) {
                    const float zoomChange = CalculateZoom(*e), rotationChange = CalculateRotation(*e);
                    const Offset panChange = CalculatePan(*e);
                    if (!pastSlop) {
                        zoom *= zoomChange;
                        rotation += rotationChange;
                        pan = pan + panChange;
                        const float size = CalculateCentroidSize(*e, false);
                        const float zoomMotion = std::abs(1 - zoom) * size;
                        const float rotationMotion = std::abs(rotation * 3.14159265358979f * size / 180.0f);
                        const float panMotion = std::hypot(pan.x, pan.y);
                        if (zoomMotion > slop || rotationMotion > slop || panMotion > slop) {
                            pastSlop = true;
                            locked = panZoomLock && rotationMotion < slop;
                        }
                    }
                    if (pastSlop) {
                        const Offset centroid = CalculateCentroid(*e, false);
                        const float effectiveRotation = locked ? 0 : rotationChange;
                        if (effectiveRotation != 0 || zoomChange != 1 || !(panChange == Offset{})) onGesture(centroid, panChange, zoomChange, effectiveRotation);
                        for (auto& c : e->changes) if (!(c.position == c.previousPosition)) c.Consume();
                    }
                }
                if (canceled || !anyPressed) break;
            }
        }
    }

}
