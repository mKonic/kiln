#include "kiln/foundation/Gestures.h"

#include "kiln/runtime/Task.h"
#include "kiln/foundation/Focus.h"

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
                m_Event = ev;
                // The event is copied, but consumption must reach the live flags: keep pointers.
                h.resume();
                // Changes made through the copy's Consume() write the shared flags directly.
            }
            void OnCancelPointerInput() override { Restart(false); }

            const ViewConfiguration& Config() const override {
                static const ViewConfiguration fallback;
                return Owner() && Owner()->GetOwner() ? Owner()->GetOwner()->Config() : fallback;
            }
            Density GetDensity() const override {
                return Owner() && Owner()->GetOwner() ? Owner()->GetOwner()->GetDensity() : Density{};
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

    Modifier Clickable(Modifier m, std::function<void()> onClick, std::function<void(bool)> onPressedChange, bool enabled) {
        if (!enabled) return m;
        // rememberUpdatedState: the running gesture reads the latest callbacks through a box the
        // composition keeps for this call site and refreshes on every recomposition.
        struct Box { std::shared_ptr<std::function<void()>> click; std::shared_ptr<std::function<void(bool)>> pressed; };
        Box box{ std::make_shared<std::function<void()>>(), std::make_shared<std::function<void(bool)>>() };
        if (auto* c = rt::Composition::Current()) box = c->Remember<Box>([&] { return box; });
        *box.click = std::move(onClick);
        *box.pressed = std::move(onPressedChange);
        auto click = box.click;
        auto pressed = box.pressed;
        // Keyboard activation (Clickable.kt CombinedClickable keys): Enter, NumpadEnter,
        // DPAD center and Space press on key down and click on key up, when focused. Clickables
        // are focusable only outside touch mode (Focusability::SystemDefined).
        m = OnKeyEvent(std::move(m), [click, pressed](const KeyEvent& e) {
            const bool activate = e.keyCode == keys::Enter || e.keyCode == keys::NumpadEnter ||
                                  e.keyCode == keys::DpadCenter || e.keyCode == keys::Space;
            if (!activate) return false;
            if (e.type == KeyEventType::KeyDown) { if (e.repeatCount == 0 && *pressed) (*pressed)(true); return true; }
            if (e.type == KeyEventType::KeyUp) { if (*pressed) (*pressed)(false); if (*click) (*click)(); return true; }
            return false;
        });
        m = Focusable(std::move(m), Focusability::SystemDefined);
        return PointerInput(std::move(m), rt::Keys(0), [click, pressed](PointerInputScope& s) {
            TapCallbacks cb;
            cb.onPress = [pressed](Offset) { if (*pressed) (*pressed)(true); };
            cb.onPressEnd = [pressed](bool) { if (*pressed) (*pressed)(false); };
            cb.onTap = [click](Offset) { if (*click) (*click)(); };
            return DetectTapGestures(s, cb);
        });
    }

}
