#pragma once

#include "kiln/foundation/Interaction.h"
#include "kiln/ui/Semantics.h"

#include <optional>
#include <string>

// Coroutine pointer input (spec 05 req. 1, §Tap): Modifier.pointerInput with a handler that
// awaits pointer events pass by pass, and the tap detector / clickable built on it.

#include "kiln/runtime/Channel.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/LayoutNode.h"

#include <coroutine>
#include <functional>
#include <optional>

namespace kiln::foundation {

    using namespace kiln::ui;

    class PointerInputScope {
    public:
        virtual ~PointerInputScope() = default;

        struct Await {
            PointerInputScope* scope;
            PointerEventPass pass;
            std::int64_t timeoutMillis;   // < 0: none
            bool await_ready() const noexcept { return false; }
            template<typename P> void await_suspend(std::coroutine_handle<P> h) {
                scope->Suspend(h, pass, timeoutMillis, h.promise().job);
            }
            std::optional<PointerEvent> await_resume() { return scope->TakeEvent(); }
        };
        // co_await AwaitPointerEvent() -> std::optional<PointerEvent> (always set without a
        // timeout). The event's changes are live: Consume() affects later passes and nodes.
        Await AwaitPointerEvent(PointerEventPass pass = PointerEventPass::Main) { return { this, pass, -1 }; }
        Await AwaitPointerEventWithTimeout(std::int64_t millis, PointerEventPass pass = PointerEventPass::Main) {
            return { this, pass, millis };
        }

        IntSize Size() const { return m_Size; }
        // The event last delivered to this handler (currentEvent).
        const PointerEvent& CurrentEvent() const { return m_Current; }
        virtual const ViewConfiguration& Config() const = 0;
        virtual Density GetDensity() const = 0;
        // Inside a scrollable container (hasScrollableContainer): press feedback waits TapTimeout.
        virtual bool InScrollableContainer() const { return false; }

    protected:
        virtual void Suspend(std::coroutine_handle<> h, PointerEventPass pass, std::int64_t timeout,
                             const std::shared_ptr<rt::JobState>& job) = 0;
        virtual std::optional<PointerEvent> TakeEvent() = 0;
        IntSize m_Size;
        PointerEvent m_Current;
    };

    using PointerInputHandler = std::function<rt::Task<>(PointerInputScope&)>;

    // DpTouchBoundsExpansion: how far a stylus or eraser reaches past a node's bounds and still
    // hits it (start / end follow the layout direction unless absolute).
    struct DpTouchBoundsExpansion {
        Dp start{ 0 }, top{ 0 }, end{ 0 }, bottom{ 0 };
        bool absolute = false;
        bool operator==(const DpTouchBoundsExpansion& o) const {
            return start.value == o.start.value && top.value == o.top.value && end.value == o.end.value && bottom.value == o.bottom.value &&
                   absolute == o.absolute;
        }
        std::array<float, 4> Resolve(const Density& d, LayoutDirection dir) const {
            const bool rtl = dir == LayoutDirection::Rtl && !absolute;
            return { d.ToPx(rtl ? end : start), d.ToPx(top), d.ToPx(rtl ? start : end), d.ToPx(bottom) };
        }
    };

    // SuspendingPointerInputModifierNode: the node behind PointerInput, for a node of one's own
    // that handles pointer input with a coroutine (derive from it). The handler starts on the
    // first event and restarts when the input is cancelled or the handler is reset.
    class SuspendingPointerInputModifierNode : public ModifierNode, public PointerInputModifier, public PointerInputScope {
    public:
        explicit SuspendingPointerInputModifierNode(PointerInputHandler handler);
        // pointerInputEventHandler: setting it cancels the running handler.
        const PointerInputHandler& PointerInputEventHandler() const { return m_Handler; }
        void SetPointerInputEventHandler(PointerInputHandler handler);
        // resetPointerInputHandler: cancels the running handler; the next event starts it anew.
        void ResetPointerInputHandler();

        PointerInputModifier* AsPointerInput() override { return this; }
        void OnDetach() override { Stop(); }
        void OnPointerEvent(PointerEvent& ev, PointerEventPass pass, IntSize bounds) override;
        void OnCancelPointerInput() override { Restart(false); }
        const ViewConfiguration& Config() const override;
        Density GetDensity() const override;
        bool InScrollableContainer() const override;

    protected:
        void Suspend(std::coroutine_handle<> h, PointerEventPass pass, std::int64_t timeout, const std::shared_ptr<rt::JobState>& job) override;
        std::optional<PointerEvent> TakeEvent() override;
        // Replaces the handler without cancelling the one running (pointerInput's equal keys).
        void SetHandlerKeepingRun(PointerInputHandler handler) { m_Handler = std::move(handler); }

    private:
        void Stop();
        void Restart(bool start);
        PointerInputHandler m_Handler;
        rt::Job m_Job;
        std::shared_ptr<rt::JobState> m_JobState;
        std::coroutine_handle<> m_Waiter;
        PointerEventPass m_WaitPass = PointerEventPass::Main;
        std::optional<PointerEvent> m_Event;
    };

    // The handler starts on the first event and restarts when cancelled or when keys change.
    Modifier PointerInput(Modifier m, rt::Keys keys, PointerInputHandler handler);
    Modifier PointerInput(Modifier m, rt::Keys keys, PointerInputHandler handler, DpTouchBoundsExpansion touchBoundsExpansion);

    // --- awaitEachGesture building blocks (AwaitPointerEventScope extensions) ----------------

    // awaitAllPointersUp (the end of awaitEachGesture): returns at once when no pointer is down in
    // the current event, else waits (final pass) until every pointer is up.
    rt::Task<> AwaitAllPointersUp(PointerInputScope& scope);

    // awaitFirstDown: the next event where every change went down.
    rt::Task<std::optional<PointerInputChange>> AwaitFirstDown(PointerInputScope& scope, bool requireUnconsumed = true,
                                                               PointerEventPass pass = PointerEventPass::Main);
    // awaitTouchSlopOrCancellation: the change that moved past the pointer's slop (onSlop gets
    // it and the over-slop distance; it must consume it to accept), or nullopt when the pointer
    // lifted or someone else consumed the motion. Follows another pressed pointer if this lifts.
    rt::Task<std::optional<PointerInputChange>> AwaitTouchSlopOrCancellation(PointerInputScope& scope, std::int64_t pointerId,
                                                                             std::function<void(PointerInputChange&, Offset overSlop)> onSlop);
    // drag(pointerId, onDrag): each move until the pointer lifts (returns the up) or someone else
    // consumed a move (nullopt).
    rt::Task<std::optional<PointerInputChange>> DragPointer(PointerInputScope& scope, std::int64_t pointerId,
                                                            std::function<void(PointerInputChange&)> onDrag);
    // waitForUpOrCancellation(pass): the up once every pointer lifted, nullopt when a change was
    // consumed (on `pass`, or its position on the Final pass) or left the bounds.
    rt::Task<std::optional<PointerInputChange>> WaitForUpOrCancellation(PointerInputScope& scope,
                                                                        PointerEventPass pass = PointerEventPass::Main);
    // withTimeout(millis) { waitForUpOrCancellation(pass) }: timedOut when neither came in time
    // (millis < 0: no timeout).
    struct UpOrCancellation {
        std::optional<PointerInputChange> up;
        bool timedOut = false;
    };
    rt::Task<UpOrCancellation> WaitForUpOrCancellationWithTimeout(PointerInputScope& scope, std::int64_t timeoutMillis,
                                                                  PointerEventPass pass = PointerEventPass::Main);
    // awaitDragOrCancellation: the next move of the pointer (following another pressed one if it
    // lifts) or its up; nullopt when it already lifted or someone else consumed the change.
    // The axis variants wait for movement along that axis.
    rt::Task<std::optional<PointerInputChange>> AwaitDragOrCancellation(PointerInputScope& scope, std::int64_t pointerId);
    rt::Task<std::optional<PointerInputChange>> AwaitHorizontalDragOrCancellation(PointerInputScope& scope, std::int64_t pointerId);
    rt::Task<std::optional<PointerInputChange>> AwaitVerticalDragOrCancellation(PointerInputScope& scope, std::int64_t pointerId);
    // await{Horizontal,Vertical}{Touch,Pointer}SlopOrCancellation: awaitTouchSlopOrCancellation
    // along one axis (the over-slop distance on it); the Pointer forms take the slop of a pointer
    // type (mice drag sooner).
    rt::Task<std::optional<PointerInputChange>> AwaitHorizontalTouchSlopOrCancellation(PointerInputScope& scope, std::int64_t pointerId,
                                                                                       std::function<void(PointerInputChange&, float overSlop)> onSlop);
    rt::Task<std::optional<PointerInputChange>> AwaitVerticalTouchSlopOrCancellation(PointerInputScope& scope, std::int64_t pointerId,
                                                                                     std::function<void(PointerInputChange&, float overSlop)> onSlop);
    rt::Task<std::optional<PointerInputChange>> AwaitHorizontalPointerSlopOrCancellation(PointerInputScope& scope, std::int64_t pointerId, PointerType type,
                                                                                         std::function<void(PointerInputChange&, float overSlop)> onSlop);
    rt::Task<std::optional<PointerInputChange>> AwaitVerticalPointerSlopOrCancellation(PointerInputScope& scope, std::int64_t pointerId, PointerType type,
                                                                                       std::function<void(PointerInputChange&, float overSlop)> onSlop);
    // horizontalDrag / verticalDrag: each move along the axis until the pointer lifts (true) or a
    // change is consumed elsewhere (false).
    rt::Task<bool> HorizontalDrag(PointerInputScope& scope, std::int64_t pointerId, std::function<void(PointerInputChange&)> onDrag);
    rt::Task<bool> VerticalDrag(PointerInputScope& scope, std::int64_t pointerId, std::function<void(PointerInputChange&)> onDrag);
    // awaitLongPressOrCancellation: the pressed change once the long-press timeout passes (or a
    // deep press), nullopt when it lifted, was consumed or left the bounds first.
    rt::Task<std::optional<PointerInputChange>> AwaitLongPressOrCancellation(PointerInputScope& scope, std::int64_t pointerId);

    // --- detectors (TapGestureDetector.kt) ---------------------------------------------------

    // PressGestureScope: what a scoped onPress waits on. tryAwaitRelease: true when the press
    // ended in a release, false when it was cancelled. awaitRelease: returns on a release; a
    // cancelled press cancels the onPress task instead (Compose throws there).
    class PressGestureScope : public ui::Density {
    public:
        rt::Task<bool> TryAwaitRelease();
        rt::Task<> AwaitRelease();
        // internal
        void End(bool released);
        rt::Job job;

    private:
        std::optional<bool> m_Result;
        std::shared_ptr<rt::Channel<bool>> m_Ended = std::make_shared<rt::Channel<bool>>(1);
    };

    struct TapCallbacks {
        std::function<void(Offset)> onPress;            // down
        std::function<void(bool released)> onPressEnd;  // up (true) or cancel (false)
        // onPress with a PressGestureScope (detectTapGestures' suspending onPress): runs in its
        // own task from the down.
        std::function<rt::Task<>(PressGestureScope&, Offset)> onPressScoped;
        std::function<void(Offset)> onTap;
        std::function<void(Offset)> onDoubleTap;
        std::function<void(Offset)> onLongPress;
    };
    rt::Task<> DetectTapGestures(PointerInputScope& scope, TapCallbacks callbacks);

    // --- drag (DragGestureDetector.kt) ------------------------------------------------------

    enum class Orientation : std::uint8_t { Vertical, Horizontal };

    // ViewConfiguration.pointerSlop: mice use touchSlop * (0.125dp / 18dp).
    float PointerSlop(const ViewConfiguration& c, PointerType type);

    struct DragCallbacks {
        std::function<void(const PointerInputChange& down, const PointerInputChange& slopTrigger, Offset overSlop)> onDragStart;
        std::function<void(const PointerInputChange& change, Offset dragAmount)> onDrag;
        std::function<void(const PointerInputChange& up)> onDragEnd;
        std::function<void()> onDragCancel;
    };
    // detectDragGestures(orientationLock): slop, then drag until up or consumed by someone else.
    rt::Task<> DetectDragGestures(PointerInputScope& scope, std::optional<Orientation> orientationLock, DragCallbacks callbacks);
    inline rt::Task<> DetectHorizontalDragGestures(PointerInputScope& s, DragCallbacks cb) { return DetectDragGestures(s, Orientation::Horizontal, std::move(cb)); }
    inline rt::Task<> DetectVerticalDragGestures(PointerInputScope& s, DragCallbacks cb) { return DetectDragGestures(s, Orientation::Vertical, std::move(cb)); }
    // detectDragGesturesAfterLongPress: a press held still for the long-press timeout, then a
    // drag without slop. onDragStart gets the long-press change as both arguments.
    rt::Task<> DetectDragGesturesAfterLongPress(PointerInputScope& scope, DragCallbacks callbacks);

    // TransformGestureDetector.kt: centroid, pan, zoom and rotation (degrees) of multi-touch.
    Offset CalculateCentroid(const PointerEvent& e, bool useCurrent = true);   // NaN x/y when none
    float CalculateCentroidSize(const PointerEvent& e, bool useCurrent = true);
    Offset CalculatePan(const PointerEvent& e);
    float CalculateZoom(const PointerEvent& e);
    float CalculateRotation(const PointerEvent& e);
    // detectTransformGestures(panZoomLock, onGesture(centroid, pan, zoom, rotation)).
    rt::Task<> DetectTransformGestures(PointerInputScope& scope, bool panZoomLock,
                                       std::function<void(Offset centroid, Offset pan, float zoom, float rotation)> onGesture);

    struct ClickableOptions {
        bool enabled = true;
        std::optional<ui::Role> role;
        std::optional<std::string> onClickLabel;
        // Null: one is created when an indication needs it.
        std::shared_ptr<MutableInteractionSource> interactionSource;
        // Null with useLocalIndication: LocalIndication (DefaultDebugIndication when unset).
        IndicationPtr indication;
        bool useLocalIndication = true;
        // combinedClickable: a long press and/or a double tap besides the click (with a double
        // tap set, a single tap waits out the double-tap timeout).
        std::function<void()> onLongClick;
        std::optional<std::string> onLongClickLabel;
        std::function<void()> onDoubleClick;
    };
    // clickable(interactionSource, indication, enabled, onClickLabel, role, onClick): tap and
    // Enter/Space/DPAD-centre activation, press interactions (delayed TapTimeout inside scrollable
    // containers), focusable outside touch mode, merged click semantics with role and label.
    // SoundEffectOnInteraction: turns the click sound of the clickables in `content` off (or back
    // on), by wrapping LocalSoundEffect.
    void SoundEffectOnInteraction(bool enabled, std::function<void()> content);

    Modifier Clickable(Modifier m, std::function<void()> onClick, ClickableOptions options);
    // Compatibility form: pressed-state callback, local indication.
    Modifier Clickable(Modifier m, std::function<void()> onClick, std::function<void(bool)> onPressedChange = {},
                       bool enabled = true);

    // toggleable(value, onValueChange): a clickable with on/off state semantics.
    Modifier Toggleable(Modifier m, bool value, std::function<void(bool)> onValueChange, ClickableOptions options = {});
    // triStateToggleable(state, onClick): 0 off, 1 on, 2 indeterminate.
    Modifier TriStateToggleable(Modifier m, int state, std::function<void()> onClick, ClickableOptions options = {});
    // selectable(selected, onClick): a clickable with selected semantics (radio buttons, tabs).
    Modifier Selectable(Modifier m, bool selected, std::function<void()> onClick, ClickableOptions options = {});
    // selectableGroup(): a traversal group for selectable children.
    Modifier SelectableGroup(Modifier m);

}
