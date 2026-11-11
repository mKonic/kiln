#include "kiln/material3/Tooltip.h"

#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/ChildParentSemantics.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/tokens/ElevationTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/PlainTooltipTokens.h"
#include "kiln/material3/tokens/RichTooltipTokens.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <coroutine>
#include <vector>

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;
    using namespace tokens;

    // --- TooltipState (TooltipStateImpl, BasicTooltipStateImpl) ---------------------------------

    namespace internal {
        class TooltipCore : public std::enable_shared_from_this<TooltipCore> {
        public:
            // `basic`: BasicTooltipStateImpl (visibility without a transition).
            TooltipCore(bool initialIsVisible, bool isPersistent, std::shared_ptr<MutatorMutex> mutex, bool basic)
                : transition(basic ? false : initialIsVisible), basicVisible(initialIsVisible), persistent(isPersistent), basic(basic),
                  mutex(mutex ? std::move(mutex) : BasicTooltipDefaults::GlobalMutatorMutex()) {}

            bool IsVisible() const {
                return basic ? basicVisible.Get() : (transition.CurrentState() || transition.TargetState());
            }
            void SetShown() {
                if (basic) basicVisible.Set(true);
                else transition.SetTargetState(true);
            }
            void Dismiss() {
                if (basic) {
                    basicVisible.Set(false);
                    return;
                }
                transition.SetTargetState(false);
                if (persistent) CancelShow();
            }
            // job?.cancel(): the running show's finally block runs now (its mutex is free before
            // whoever cancelled it goes on); the suspended caller resumes on the next dispatch.
            void CancelShow() {
                if (!running) return;
                const Run run = std::move(*running);
                running.reset();
                timeout.Cancel();
                mutex->Release(run.token);
                if (basic) basicVisible.Set(false);
                else if (run.priority != MutatePriority::PreventUserInput) Dismiss();
                if (run.h) {
                    rt::Dispatcher::Main().Post([h = run.h, job = run.job] {
                        auto j = job.lock();
                        if (j && !j->cancelled) rt::ResumeJob(j, h);
                    });
                }
            }

            struct Run {
                std::uint64_t id = 0, token = 0;
                MutatePriority priority = MutatePriority::Default;
                std::coroutine_handle<> h;
                std::weak_ptr<rt::JobState> job;
            };

            MutableTransitionState<bool> transition;
            rt::State<bool> basicVisible;
            const bool persistent, basic;
            const std::shared_ptr<MutatorMutex> mutex;
            std::optional<Run> running;
            std::uint64_t nextRun = 0;
            rt::Job timeout;
        };
    }

    namespace {
        using internal::TooltipCore;

        // suspendCancellableCoroutine { job = it }: resumed by CancelShow. Destroying the frame
        // while suspended (the caller was cancelled) runs the show's finally.
        struct AwaitShowEnd {
            std::shared_ptr<TooltipCore> core;
            std::uint64_t id;
            bool await_ready() const noexcept { return !core->running || core->running->id != id; }
            template<typename P> bool await_suspend(std::coroutine_handle<P> h) {
                if (!core->running || core->running->id != id) return false;
                core->running->h = h;
                core->running->job = rt::detail::JobOf(h);
                rt::detail::JobOf(h)->unregister = nullptr;
                return true;
            }
            void await_resume() const noexcept {}
            ~AwaitShowEnd() {
                if (core->running && core->running->id == id) {
                    core->running->h = {};
                    core->CancelShow();
                }
            }
        };

        rt::Task<> ShowTimeout(std::weak_ptr<TooltipCore> weak, std::uint64_t id) {
            co_await rt::Delay(BasicTooltipDefaults::TooltipDuration);
            auto core = weak.lock();
            if (core && core->running && core->running->id == id) {
                // This task's own Job: freed once the task has returned, not from inside it.
                rt::Dispatcher::Main().Post([job = std::make_shared<rt::Job>(std::move(core->timeout))] {});
                core->CancelShow();
            }
        }

        rt::Task<> ShowTask(std::shared_ptr<TooltipCore> core, MutatePriority priority) {
            const std::uint64_t id = ++core->nextRun;
            std::weak_ptr<TooltipCore> weak = core;
            const std::uint64_t token = core->mutex->Acquire(priority, [weak, id] {
                auto c = weak.lock();
                if (c && c->running && c->running->id == id) c->CancelShow();
            });
            if (token == 0) co_return;   // a higher-priority show holds the mutex
            core->running = TooltipCore::Run{ id, token, priority, {}, {} };
            core->SetShown();
            // Shown for TooltipDuration, or until dismissed when persistent or hovered.
            if (!(core->persistent || priority == MutatePriority::UserInput))
                core->timeout = rt::Launch(rt::Dispatcher::Main(), ShowTimeout(weak, id));
            co_await AwaitShowEnd{ core, id };
        }
    }

    namespace BasicTooltipDefaults {
        const std::shared_ptr<MutatorMutex>& GlobalMutatorMutex() {
            static const auto mutex = std::make_shared<MutatorMutex>();
            return mutex;
        }
    }

    MutableTransitionState<bool>& TooltipState::Transition() const { return m_Core->transition; }
    bool TooltipState::IsVisible() const { return m_Core->IsVisible(); }
    bool TooltipState::IsPersistent() const { return m_Core->persistent; }
    rt::Task<> TooltipState::Show(MutatePriority priority) const { return ShowTask(m_Core, priority); }
    void TooltipState::Dismiss() const { m_Core->Dismiss(); }
    void TooltipState::OnDispose() const { m_Core->CancelShow(); }

    TooltipState MakeTooltipState(bool initialIsVisible, bool isPersistent, std::shared_ptr<MutatorMutex> mutatorMutex) {
        return TooltipState(std::make_shared<TooltipCore>(initialIsVisible, isPersistent, std::move(mutatorMutex), false));
    }
    TooltipState RememberTooltipState(bool initialIsVisible, bool isPersistent, std::shared_ptr<MutatorMutex> mutatorMutex) {
        auto& c = *rt::Composition::Current();
        return c.Remember<TooltipState>([&] { return MakeTooltipState(initialIsVisible, isPersistent, mutatorMutex); },
                                        rt::Keys(isPersistent, mutatorMutex.get()));
    }
    namespace internal {
        TooltipState MakeBasicTooltipState(bool initialIsVisible, bool isPersistent, std::shared_ptr<MutatorMutex> mutatorMutex) {
            return TooltipState(std::make_shared<TooltipCore>(initialIsVisible, isPersistent, std::move(mutatorMutex), true));
        }
        TooltipState RememberBasicTooltipState(bool initialIsVisible, bool isPersistent, std::shared_ptr<MutatorMutex> mutatorMutex) {
            auto& c = *rt::Composition::Current();
            return c.Remember<TooltipState>([&] { return MakeBasicTooltipState(initialIsVisible, isPersistent, mutatorMutex); },
                                            rt::Keys(isPersistent, mutatorMutex.get()));
        }
    }

    // --- BasicTooltipBox ---------------------------------------------------------------------

    namespace {
        rt::Task<> Run(TooltipState state, MutatePriority priority) { co_await state.Show(priority); }

        // The long press's progress, which the show waits on (the isLongPressed StateFlow).
        struct LongPressFlag {
            bool pressed = false;
            std::coroutine_handle<> waiter;
            std::weak_ptr<rt::JobState> job;
            void Set(bool v) {
                pressed = v;
                if (v || !waiter) return;
                rt::Dispatcher::Main().Post([h = std::exchange(waiter, {}), j = job] {
                    auto s = j.lock();
                    if (s && !s->cancelled) rt::ResumeJob(s, h);
                });
            }
        };
        struct AwaitReleased {
            std::shared_ptr<LongPressFlag> flag;
            bool await_ready() const noexcept { return !flag->pressed; }
            template<typename P> void await_suspend(std::coroutine_handle<P> h) {
                flag->waiter = h;
                flag->job = rt::detail::JobOf(h);
                rt::detail::JobOf(h)->unregister = nullptr;
            }
            void await_resume() const noexcept {}
            ~AwaitReleased() { flag->waiter = {}; }
        };

        // Shown for the long press; once the show ends, it stays until the finger lifts.
        rt::Task<> LongPressShow(TooltipState state, std::shared_ptr<LongPressFlag> flag) {
            flag->Set(true);
            co_await state.Show(MutatePriority::PreventUserInput);
            if (state.IsVisible()) {
                co_await AwaitReleased{ flag };
                state.Dismiss();
            }
        }

        rt::Task<> LongPressGestures(PointerInputScope& s, TooltipState state) {
            std::vector<rt::Job> children;   // the handler's coroutineScope
            for (;;) {
                // awaitEachGesture
                auto down = co_await AwaitFirstDown(s, true, PointerEventPass::Initial);
                if (down && (down->type == PointerType::Touch || down->type == PointerType::Stylus)) {
                    auto flag = std::make_shared<LongPressFlag>();
                    auto r = co_await WaitForUpOrCancellationWithTimeout(s, s.Config().longPressTimeoutMillis, PointerEventPass::Initial);
                    if (r.timedOut) {
                        std::erase_if(children, [](const rt::Job& j) { return j.Done(); });
                        children.push_back(rt::Launch(rt::Dispatcher::Main(), LongPressShow(state, flag)));
                        // The press became a long press: the anchor's own click does not happen.
                        auto up = co_await WaitForUpOrCancellation(s, PointerEventPass::Initial);
                        if (up) up->Consume();   // the copy shares the live change's consumption
                    }
                    flag->Set(false);
                }
                co_await AwaitAllPointersUp(s);
            }
        }

        rt::Task<> HoverGestures(PointerInputScope& s, TooltipState state) {
            std::vector<rt::Job> children;
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                if (e->changes.empty() || e->changes[0].type != PointerType::Mouse) continue;
                if (e->type == PointerEventType::Enter) {
                    std::erase_if(children, [](const rt::Job& j) { return j.Done(); });
                    children.push_back(rt::Launch(rt::Dispatcher::Main(), Run(state, MutatePriority::UserInput)));
                } else if (e->type == PointerEventType::Exit) {
                    if (!state.IsPersistent()) state.Dismiss();
                }
            }
        }

        rt::Task<> FocusShow(TooltipState state, std::shared_ptr<rt::State<bool>> received, bool focused) {
            // Shown while the anchor has keyboard focus.
            if (focused) {
                received->Set(true);
                co_await state.Show(MutatePriority::PreventUserInput);
            }
            if (received->Peek() && state.IsVisible() && !focused) {
                received->Set(false);
                state.Dismiss();
            }
        }
    }

    namespace internal {
        void BasicTooltipBox(std::shared_ptr<const PopupPositionProvider> positionProvider, std::function<void()> tooltip,
                             const TooltipState& state, const Modifier& modifier, const TooltipBoxOptions& options,
                             std::function<void()> content) {
            auto& c = *rt::Composition::Current();
            auto scope = internal::RememberCoroutineScope();
            auto& forceFocusableForKeyboardNav = c.Remember<std::shared_ptr<rt::State<bool>>>([] { return std::make_shared<rt::State<bool>>(false); });
            auto& receivedKeyboardFocus = c.Remember<std::shared_ptr<rt::State<bool>>>([] { return std::make_shared<rt::State<bool>>(false); });
            // Touch exploration or switch access users reach a tooltip action through focus.
            const auto manager = c.Read(LocalAccessibilityManager);
            const bool serviceOn = manager && AccessibilityServiceOn(manager->ServiceState(), true, true, false);
            const bool forceFocusable = options.hasAction && (serviceOn || forceFocusableForKeyboardNav->Get());

            Box({}, Alignment::TopStart(), false, [&] {
                if (state.IsVisible()) {
                    // TooltipPopup
                    const std::string description = GetString(Strings::TooltipPaneDescription);
                    auto onDismiss = options.onDismissRequest;
                    PopupProperties properties;
                    properties.focusable = options.focusable || forceFocusable;
                    properties.clippingEnabled = false;
                    Popup(
                        positionProvider,
                        [onDismiss, state, scopePtr = scope, force = forceFocusableForKeyboardNav] {
                            if (onDismiss) {
                                onDismiss();
                            } else if (state.IsVisible()) {
                                state.Dismiss();
                                // Keyboard focus is not trapped once the tooltip is gone.
                                force->Set(false);
                            }
                        },
                        properties,
                        [description, tooltip] {
                            Box(Semantics({}, [description](SemanticsConfiguration& config) {
                                    config.liveRegion = LiveRegionMode::Assertive;
                                    config.paneTitle = description;
                                }),
                                Alignment::TopStart(), false, tooltip);
                        });
                }

                // WrappedAnchor
                Modifier anchor = modifier;
                if (options.enableUserInput) {
                    anchor = PointerInput(std::move(anchor), rt::Keys(static_cast<const void*>(&state.Transition())),
                                          [state](PointerInputScope& s) { return LongPressGestures(s, state); });
                    anchor = PointerInput(std::move(anchor), rt::Keys(static_cast<const void*>(&state.Transition())),
                                          [state](PointerInputScope& s) { return HoverGestures(s, state); });
                    const std::string longPressLabel = GetString(Strings::TooltipLongPressLabel);
                    anchor = ParentSemantics(std::move(anchor), [state, scopePtr = scope, longPressLabel](SemanticsConfiguration& config) {
                        config.onLongClickLabel = longPressLabel;
                        config.onLongClick = [state, scopePtr] {
                            scopePtr->Launch(Run(state, MutatePriority::Default));
                            return true;
                        };
                    });
                    anchor = OnFocusChanged(std::move(anchor), [state, scopePtr = scope, received = receivedKeyboardFocus](FocusState f) {
                        scopePtr->Launch(FocusShow(state, received, IsFocused(f)));
                    });
                    const bool hasAction = options.hasAction;
                    anchor = OnKeyEvent(
                        std::move(anchor), {},
                        [state, hasAction, force = forceFocusableForKeyboardNav, received = receivedKeyboardFocus](const KeyEvent& e) {
                            if (!state.IsVisible()) {
                                force->Set(false);
                                return false;
                            }
                            // Tab from the anchor moves to a tooltip with an action.
                            if (hasAction && !e.Shift() && e.type == KeyEventType::KeyDown && e.keyCode == keys::Tab) {
                                force->Set(true);
                                return true;
                            }
                            // Escape dismisses the tooltip.
                            if (e.type == KeyEventType::KeyDown && e.keyCode == keys::Escape) {
                                received->Set(false);
                                state.Dismiss();
                                return true;
                            }
                            return false;
                        });
                } else if (forceFocusableForKeyboardNav->Peek()) {
                    forceFocusableForKeyboardNav->Set(false);
                }
                Box(anchor, Alignment::TopStart(), false, content);
            });

            c.DisposableEffect(rt::Keys(static_cast<const void*>(&state.Transition())), [state] {
                return std::function<void()>([state] { state.OnDispose(); });
            });
        }
    }

    // --- position providers ------------------------------------------------------------------

    namespace {
        class TooltipPositionProviderImpl final : public PopupPositionProvider {
        public:
            TooltipPositionProviderImpl(TooltipAnchorPosition type, int spacing, IntSize windowContainerSize)
                : type(type), spacing(spacing), window(windowContainerSize) {}
            IntOffset CalculatePosition(IntRect anchor, IntSize, LayoutDirection dir, IntSize popup) const override {
                switch (type) {
                    case TooltipAnchorPosition::Left: return Left(anchor, popup);
                    case TooltipAnchorPosition::Right: return Right(anchor, popup);
                    case TooltipAnchorPosition::Below: return Below(anchor, popup);
                    case TooltipAnchorPosition::Start: return dir == LayoutDirection::Ltr ? Left(anchor, popup) : Right(anchor, popup);
                    case TooltipAnchorPosition::End: return dir == LayoutDirection::Ltr ? Right(anchor, popup) : Left(anchor, popup);
                    default: return Above(anchor, popup);
                }
            }
            const TooltipAnchorPosition type;

        private:
            int CoerceX(int x, IntSize popup) const { return std::clamp(x, 0, std::max(0, window.width - popup.width)); }
            int CoerceY(int y, IntSize popup) const { return std::clamp(y, 0, std::max(0, window.height - popup.height)); }
            // Left of the anchor, flipped right when it does not fit; centred vertically.
            IntOffset Left(IntRect a, IntSize p) const {
                int x = a.left - (p.width + spacing);
                if (x < 0) x = a.right + spacing;
                return { CoerceX(x, p), CoerceY((a.top + a.bottom - p.height) / 2, p) };
            }
            IntOffset Right(IntRect a, IntSize p) const {
                int x = a.right + spacing;
                if (x + p.width > window.width) x = a.left - (p.width + spacing);
                return { CoerceX(x, p), CoerceY((a.top + a.bottom - p.height) / 2, p) };
            }
            // Centred above the anchor, below when it would go off the top.
            IntOffset Above(IntRect a, IntSize p) const {
                const int x = a.left + ((a.right - a.left) - p.width) / 2;
                int y = a.top - p.height - spacing;
                if (y < 0) y = a.bottom + spacing;
                return { CoerceX(x, p), CoerceY(y, p) };
            }
            IntOffset Below(IntRect a, IntSize p) const {
                const int x = a.left + ((a.right - a.left) - p.width) / 2;
                int y = a.bottom + spacing;
                if (y + p.height > window.height) y = a.top - p.height - spacing;
                return { CoerceX(x, p), CoerceY(y, p) };
            }
            const int spacing;
            const IntSize window;
        };

        // The deprecated providers (rememberPlain / rememberRich / rememberTooltipPositionProvider(spacing)).
        class LegacyTooltipPositionProvider final : public PopupPositionProvider {
        public:
            enum class Kind { Plain, Rich, Default };
            LegacyTooltipPositionProvider(Kind kind, int spacing) : kind(kind), spacing(spacing) {}
            IntOffset CalculatePosition(IntRect a, IntSize window, LayoutDirection, IntSize p) const override {
                int x = a.left + ((a.right - a.left) - p.width) / 2;
                if (kind == Kind::Rich) {
                    // Start-aligned; end-aligned when it would leave the right edge, centred when
                    // that leaves the left one.
                    x = a.left;
                    if (x + p.width > window.width) {
                        x = a.right - p.width;
                        if (x < 0) x = a.left + ((a.right - a.left) - p.width) / 2;
                    }
                } else if (kind == Kind::Default) {
                    if (x < 0) x = a.left;
                    else if (x + p.width > window.width) x = a.right - p.width;
                }
                int y = a.top - p.height - spacing;
                if (y < 0) y = a.bottom + spacing;
                return { x, y };
            }

        private:
            const Kind kind;
            const int spacing;
        };

        int RoundToPx(Dp dp) {
            return rt::Composition::Current()->Read(LocalDensity).RoundToPx(dp);
        }
        IntSize WindowContainerSize() {
            const Owner* owner = rt::Composition::Current()->Read(LocalOwner);
            return owner ? owner->ObservedWindowSize() : IntSize{};
        }
        std::shared_ptr<const PopupPositionProvider> RememberLegacy(LegacyTooltipPositionProvider::Kind kind, Dp spacing) {
            const int px = RoundToPx(spacing);
            return rt::Composition::Current()->Remember<std::shared_ptr<const PopupPositionProvider>>(
                [&] { return std::make_shared<LegacyTooltipPositionProvider>(kind, px); }, rt::Keys(px));
        }
    }

    namespace TooltipDefaults {
        ShapePtr plainTooltipContainerShape() { return Value(PlainTooltipTokens::ContainerShape); }
        Color plainTooltipContainerColor() { return Value(PlainTooltipTokens::ContainerColor); }
        Color plainTooltipContentColor() { return Value(PlainTooltipTokens::SupportingTextColor); }
        ShapePtr richTooltipContainerShape() { return Value(RichTooltipTokens::ContainerShape); }
        ShapePtr caretShape(DpSize size) {
            static const ShapePtr defaultCaret = std::make_shared<DefaultTooltipCaretShape>(TooltipDefaults::caretSize);
            if (size == TooltipDefaults::caretSize) return defaultCaret;
            return std::make_shared<DefaultTooltipCaretShape>(size);
        }
        RichTooltipColors richTooltipColors() {
            return { Value(RichTooltipTokens::ContainerColor), Value(RichTooltipTokens::SupportingTextColor),
                     Value(RichTooltipTokens::SubheadColor), Value(RichTooltipTokens::ActionLabelTextColor) };
        }
        RichTooltipColors richTooltipColors(Color containerColor, Color contentColor, Color titleContentColor, Color actionContentColor) {
            return richTooltipColors().Copy(containerColor, contentColor, titleContentColor, actionContentColor);
        }
        std::shared_ptr<const PopupPositionProvider> rememberTooltipPositionProvider(TooltipAnchorPosition positioning, Dp spacing) {
            const int px = RoundToPx(spacing);
            const IntSize window = WindowContainerSize();
            return rt::Composition::Current()->Remember<std::shared_ptr<const PopupPositionProvider>>(
                [&] { return std::make_shared<TooltipPositionProviderImpl>(positioning, px, window); },
                rt::Keys(px, int(positioning), window.width, window.height));
        }
        std::shared_ptr<const PopupPositionProvider> rememberTooltipPositionProvider(Dp spacing) {
            return RememberLegacy(LegacyTooltipPositionProvider::Kind::Default, spacing);
        }
        std::shared_ptr<const PopupPositionProvider> rememberPlainTooltipPositionProvider(Dp spacing) {
            return RememberLegacy(LegacyTooltipPositionProvider::Kind::Plain, spacing);
        }
        std::shared_ptr<const PopupPositionProvider> rememberRichTooltipPositionProvider(Dp spacing) {
            return RememberLegacy(LegacyTooltipPositionProvider::Kind::Rich, spacing);
        }
    }

    RichTooltipColors RichTooltipColors::Copy(Color container, Color content, Color title, Color action) const {
        return { container.Specified() ? container : containerColor, content.Specified() ? content : contentColor,
                 title.Specified() ? title : titleContentColor, action.Specified() ? action : actionContentColor };
    }

    // --- carets ------------------------------------------------------------------------------

    DefaultTooltipCaretShape::DefaultTooltipCaretShape(DpSize caretSize) : m_CaretSize(caretSize) {}
    gfx::Outline DefaultTooltipCaretShape::CreateOutline(Size, LayoutDirection, const Density& density) const {
        const float w = density.ToPx(m_CaretSize.width), h = density.ToPx(m_CaretSize.height);
        gfx::Path path;
        path.MoveTo(0, 0);
        path.LineTo(w / 2, 0);
        path.LineTo(0, h);
        path.LineTo(-w / 2, 0);
        path.Close();
        return { std::move(path) };
    }

    namespace internal {
        float CaretX(float tooltipWidth, int screenWidthPx, Rect anchorBounds) {
            const float anchorLeft = anchorBounds.left, anchorRight = anchorBounds.right;
            const float anchorMid = (anchorLeft + anchorRight) / 2;
            const float screen = float(screenWidthPx);
            // As wide as the screen: under the anchor's middle.
            if (tooltipWidth >= screen) return anchorMid;
            // Start-aligned against the left edge, corrected when it also meets the right one.
            if (anchorMid - tooltipWidth / 2 < 0) return anchorMid + std::max(tooltipWidth - screen, -anchorLeft);
            // End-aligned against the right edge, corrected when it also meets the left one.
            if (anchorMid + tooltipWidth / 2 > screen) return anchorMid + std::min(tooltipWidth - anchorRight, 0.0f);
            return tooltipWidth / 2;
        }
    }

    namespace {
        bool SameMatrix(const gfx::Matrix& x, const gfx::Matrix& y) {
            return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d && x.e == y.e && x.f == y.f;
        }

        // TooltipCaretShape: the tooltip's shape joined with the caret moved and turned by the matrix.
        class TooltipCaretShape final : public gfx::Shape {
        public:
            TooltipCaretShape(gfx::Matrix matrix, ShapePtr tooltipShape, ShapePtr caretShape)
                : m_Matrix(matrix), m_Tooltip(std::move(tooltipShape)), m_Caret(std::move(caretShape)) {}
            gfx::Outline CreateOutline(Size size, LayoutDirection dir, const Density& density) const override {
                const gfx::Path tooltip = m_Tooltip->CreateOutline(size, dir, density).ToPath();
                gfx::Path caret = m_Caret->CreateOutline(size, dir, density).ToPath();
                caret.Transform(m_Matrix);
                return { gfx::Path::Combine(gfx::PathOperation::Union, tooltip, caret) };
            }

        private:
            gfx::Matrix m_Matrix;
            ShapePtr m_Tooltip, m_Caret;
        };

        // Where the caret sits on the tooltip and which way it points, from where the tooltip
        // landed next to its anchor (layoutCaret).
        gfx::Matrix CaretMatrix(const LayoutCoordinates& tooltip, const LayoutCoordinates& anchor, const PopupPositionProvider* provider,
                                LayoutDirection dir, int screenWidthPx) {
            const Offset tooltipPos = tooltip.PositionInRoot(), anchorPos = anchor.PositionInRoot();
            const IntSize tooltipSize = tooltip.GetSize(), anchorSize = anchor.GetSize();
            const float width = float(tooltipSize.width), height = float(tooltipSize.height);
            const Rect anchorBounds{ anchorPos.x, anchorPos.y, anchorPos.x + float(anchorSize.width), anchorPos.y + float(anchorSize.height) };
            const bool isBelow = tooltipPos.y > anchorPos.y, isToTheRight = tooltipPos.x > anchorPos.x;
            const auto* impl = dynamic_cast<const TooltipPositionProviderImpl*>(provider);
            const TooltipAnchorPosition type = impl ? impl->type : TooltipAnchorPosition::Above;
            const bool ltr = dir == LayoutDirection::Ltr;
            // Start / End resolve to Left / Right.
            TooltipAnchorPosition side = type;
            if (type == TooltipAnchorPosition::Start) side = ltr ? TooltipAnchorPosition::Left : TooltipAnchorPosition::Right;
            if (type == TooltipAnchorPosition::End) side = ltr ? TooltipAnchorPosition::Right : TooltipAnchorPosition::Left;
            const bool horizontal = side == TooltipAnchorPosition::Left || side == TooltipAnchorPosition::Right;
            const float caretY = horizontal ? height / 2 : (isBelow ? 0.0f : height);
            const float caretX = horizontal ? (isToTheRight ? 0.0f : width) : internal::CaretX(width, screenWidthPx, anchorBounds);
            gfx::Matrix m = gfx::Matrix::Translate(caretX, caretY);
            if (horizontal) {
                // rotateZ(90) when the tooltip is right of the anchor, rotateZ(-90) when left.
                const float s = isToTheRight ? 1.0f : -1.0f;
                m.a = 0;
                m.b = s;
                m.c = -s;
                m.d = 0;
            } else if (isBelow) {
                m.d = -1;   // rotateX(180): the caret points up
            }
            return m;
        }

        class TooltipScopeImpl final : public TooltipScope {
        public:
            std::optional<LayoutCoordinates> ObtainAnchorBounds() const override { return anchorBounds ? anchorBounds->Get() : std::nullopt; }
            std::shared_ptr<const PopupPositionProvider> ObtainPositionProvider() const override { return positionProvider; }
            std::shared_ptr<rt::State<std::optional<LayoutCoordinates>>> anchorBounds;
            std::shared_ptr<const PopupPositionProvider> positionProvider;
        };

        // The caret modifier and shape when `caretShape` is set.
        std::pair<Modifier, ShapePtr> WithCaret(const TooltipScope& scope, const Modifier& modifier, ShapePtr shape, ShapePtr caretShape) {
            if (!caretShape) return { modifier, shape };
            auto& c = *rt::Composition::Current();
            auto& matrix = c.Remember<std::shared_ptr<rt::State<gfx::Matrix, rt::NeverEqual>>>([] { return std::make_shared<rt::State<gfx::Matrix, rt::NeverEqual>>(gfx::Matrix{}); });
            const LayoutDirection dir = c.Read(LocalLayoutDirection);
            const int screenWidth = WindowContainerSize().width;
            const auto provider = scope.ObtainPositionProvider();
            const auto anchorOf = [&scope] { return scope.ObtainAnchorBounds(); };
            Modifier m = OnGloballyPositioned({}, [matrix, anchorOf, provider, dir, screenWidth](const LayoutCoordinates& tooltip) {
                const auto anchor = anchorOf();
                if (!anchor || !anchor->IsAttached()) return;
                const gfx::Matrix next = CaretMatrix(tooltip, *anchor, provider.get(), dir, screenWidth);
                if (!SameMatrix(next, matrix->Peek())) matrix->Set(next);
            });
            m = m.Then(modifier);
            const gfx::Matrix current = matrix->Get();
            return { std::move(m), std::make_shared<TooltipCaretShape>(current, std::move(shape), std::move(caretShape)) };
        }

        void ProvideColorAndStyle(Color color, const text::TextStyle& style, std::function<void()> content) {
            auto& c = *rt::Composition::Current();
            c.Provide(LocalContentColor, color, [&c, style, content = std::move(content)] { c.Provide(LocalTextStyle(), style, content); });
        }

        constexpr Dp TooltipMinHeight{ 24 }, TooltipMinWidth{ 40 };
        constexpr Dp PlainTooltipVerticalPadding{ 4 }, PlainTooltipHorizontalPadding{ 8 };
        constexpr Dp RichTooltipHorizontalPadding{ 16 }, HeightToSubheadFirstLine{ 28 }, HeightFromSubheadToTextFirstLine{ 24 },
            TextBottomPadding{ 16 }, ActionLabelMinHeight{ 36 }, ActionLabelBottomPadding{ 8 };

        Modifier TextVerticalPadding(Modifier m, bool subheadExists, bool actionExists) {
            if (!subheadExists && !actionExists) return Padding(std::move(m), Dp(0), PlainTooltipVerticalPadding);
            return Padding(PaddingFromBaseline(std::move(m), HeightFromSubheadToTextFirstLine), PaddingValues{ Dp(0), Dp(0), Dp(0), TextBottomPadding });
        }
    }

    // --- TooltipBox ----------------------------------------------------------------------------

    void TooltipBox(std::shared_ptr<const PopupPositionProvider> positionProvider, std::function<void(const TooltipScope&)> tooltip,
                    const TooltipState& state, const Modifier& modifier, const TooltipBoxOptions& options, std::function<void()> content) {
        auto& c = *rt::Composition::Current();
        auto& transition = UpdateTransition(state.Transition());
        auto& anchorBounds = c.Remember<std::shared_ptr<rt::State<std::optional<LayoutCoordinates>>>>(
            [] { return std::make_shared<rt::State<std::optional<LayoutCoordinates>>>(std::nullopt); });
        auto& scope = c.Remember<std::shared_ptr<TooltipScopeImpl>>([] { return std::make_shared<TooltipScopeImpl>(); });
        scope->anchorBounds = anchorBounds;
        scope->positionProvider = positionProvider;
        auto& tooltipPosition = c.Remember<std::shared_ptr<rt::State<std::optional<Offset>>>>(
            [] { return std::make_shared<rt::State<std::optional<Offset>>>(std::nullopt); });

        // Which side of the anchor the tooltip landed on: a new side restarts its content.
        int tooltipSide = 0;
        const auto anchor = anchorBounds->Get();
        const auto popupPosition = tooltipPosition->Get();
        if (anchor && popupPosition) {
            const Offset anchorPosition = anchor->PositionInRoot();
            if (popupPosition->x <= anchorPosition.x) tooltipSide = popupPosition->y < anchorPosition.y ? 1 : 3;
            else tooltipSide = popupPosition->y < anchorPosition.y ? 2 : 4;
        }

        const anim::SpecPtr scaleSpec = Value(MotionSchemeKeyTokens::FastSpatial), alphaSpec = Value(MotionSchemeKeyTokens::FastEffects);
        const float scale = AnimateFloat<bool>(transition, [](const bool& v) { return v ? 1.0f : 0.8f; }, [scaleSpec](const auto&) { return scaleSpec; });
        const float alpha = AnimateFloat<bool>(transition, [](const bool& v) { return v ? 1.0f : 0.0f; }, [alphaSpec](const auto&) { return alphaSpec; });

        auto wrappedContent = [anchorBounds, content = std::move(content)] {
            Box(OnGloballyPositioned({}, [anchorBounds](const LayoutCoordinates& coordinates) {
                    if (!(anchorBounds->Peek() == std::optional(coordinates))) anchorBounds->Set(coordinates);
                }),
                Alignment::TopStart(), false, content);
        };
        auto wrappedTooltip = [tooltipPosition, tooltipSide, scale, alpha, scopePtr = scope, tooltip = std::move(tooltip)] {
            Box(OnGloballyPositioned({}, [tooltipPosition](const LayoutCoordinates& coordinates) {
                    const Offset p = coordinates.PositionInRoot();
                    if (tooltipPosition->Peek() != std::optional(p)) tooltipPosition->Set(p);
                }),
                Alignment::TopStart(), false, [&] {
                    Box(GraphicsLayer({}, [scale, alpha](GraphicsLayerParams& p) {
                            p.scaleX = scale;
                            p.scaleY = scale;
                            p.alpha = alpha;
                        }),
                        Alignment::TopStart(), false, [&] { tooltip(*scopePtr); }, rt::Key(tooltipSide));
                });
        };
        internal::BasicTooltipBox(std::move(positionProvider), wrappedTooltip, state, modifier, options, wrappedContent);
    }

    // --- PlainTooltip / RichTooltip ------------------------------------------------------------

    void PlainTooltip(const TooltipScope& scope, const Modifier& modifier, const PlainTooltipOptions& o, std::function<void()> content) {
        const ShapePtr shape = o.shape ? o.shape : TooltipDefaults::plainTooltipContainerShape();
        auto [tooltipModifier, tooltipShape] = WithCaret(scope, modifier, shape, o.caretShape);
        const Color contentColor = o.contentColor ? *o.contentColor : TooltipDefaults::plainTooltipContentColor();
        SurfaceOptions surface;
        surface.shape = tooltipShape;
        surface.color = o.containerColor ? *o.containerColor : TooltipDefaults::plainTooltipContainerColor();
        surface.tonalElevation = o.tonalElevation;
        surface.shadowElevation = o.shadowElevation;
        const Dp maxWidth = o.maxWidth;
        Surface(tooltipModifier, surface, [maxWidth, contentColor, content = std::move(content)] {
            const Modifier box = Padding(SizeIn({}, TooltipMinWidth, TooltipMinHeight, maxWidth, Dp::Unspecified()), PlainTooltipHorizontalPadding,
                                         PlainTooltipVerticalPadding);
            Box(box, Alignment::TopStart(), false, [&] { ProvideColorAndStyle(contentColor, Value(PlainTooltipTokens::SupportingTextFont), content); });
        });
    }

    void RichTooltip(const TooltipScope& scope, const Modifier& modifier, const RichTooltipOptions& o, std::function<void()> text) {
        const ShapePtr shape = o.shape ? o.shape : TooltipDefaults::richTooltipContainerShape();
        auto [tooltipModifier, tooltipShape] = WithCaret(scope, modifier, shape, o.caretShape);
        const RichTooltipColors colors = o.colors ? *o.colors : TooltipDefaults::richTooltipColors();
        SurfaceOptions surface;
        surface.shape = tooltipShape;
        surface.color = colors.containerColor;
        surface.tonalElevation = o.tonalElevation;
        surface.shadowElevation = o.shadowElevation ? *o.shadowElevation : RichTooltipTokens::ContainerElevation;
        auto title = o.title, action = o.action;
        Surface(SizeIn(tooltipModifier, TooltipMinWidth, TooltipMinHeight, o.maxWidth, Dp::Unspecified()), surface,
                [colors, title, action, text = std::move(text)] {
                    const text::TextStyle actionStyle = Value(RichTooltipTokens::ActionLabelTextFont), subheadStyle = Value(RichTooltipTokens::SubheadFont),
                                          supportingStyle = Value(RichTooltipTokens::SupportingTextFont);
                    Column(Padding({}, RichTooltipHorizontalPadding, Dp(0)), Arrangement::Top(), AlignmentH::Start(), [&] {
                        if (title) {
                            Box(PaddingFromBaseline({}, HeightToSubheadFirstLine), Alignment::TopStart(), false,
                                [&] { ProvideColorAndStyle(colors.titleContentColor, subheadStyle, title); });
                        }
                        Box(TextVerticalPadding({}, bool(title), bool(action)), Alignment::TopStart(), false,
                            [&] { ProvideColorAndStyle(colors.contentColor, supportingStyle, text); });
                        if (action) {
                            Box(Padding(SizeIn({}, Dp::Unspecified(), ActionLabelMinHeight, Dp::Unspecified(), Dp::Unspecified(), false), PaddingValues{ Dp(0), Dp(0), Dp(0), ActionLabelBottomPadding }),
                                Alignment::TopStart(), false, [&] { ProvideColorAndStyle(colors.actionContentColor, actionStyle, action); });
                        }
                    });
                });
    }

    namespace internal {
        void PlainTooltipInternal(const TooltipScope& scope, const std::string& tooltipText, const Modifier& modifier,
                                  const PlainTooltipOptions& options, std::function<void()> content) {
            Modifier m = Semantics({}, [tooltipText](SemanticsConfiguration& config) {
                config.liveRegion = LiveRegionMode::Assertive;
                config.paneTitle = tooltipText;
            });
            PlainTooltip(scope, m.Then(modifier), options, std::move(content));
        }
    }

}
