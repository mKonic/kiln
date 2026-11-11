#include "kiln/material3/Snackbar.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/Button.h"
#include "kiln/material3/IconButton.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/Tooltip.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/Icons.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/SnackbarTokens.h"
#include "kiln/ui/Layout.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <climits>

namespace kiln::material3 {
    using namespace foundation;
    using ui::Dp;
    namespace T = tokens::SnackbarTokens;

    // --- the queue ------------------------------------------------------------------------------

    namespace internal {
        // The fair Mutex of showSnackbar, and SnackbarDataImpl's continuation. Resumptions go
        // through the dispatcher, as a resumed continuation does.
        class SnackbarQueue {
        public:
            struct Waiter { std::coroutine_handle<> h; std::weak_ptr<rt::JobState> job; };
            bool locked = false;
            std::deque<std::shared_ptr<Waiter>> waiters;
            std::shared_ptr<char> alive = std::make_shared<char>(0);

            struct Lock {
                SnackbarQueue* q;
                bool await_ready() {
                    if (q->locked) return false;
                    q->locked = true;
                    return true;
                }
                template<typename P> void await_suspend(std::coroutine_handle<P> h) {
                    auto job = rt::detail::JobOf(h);
                    auto w = std::make_shared<Waiter>(Waiter{ h, job });
                    q->waiters.push_back(w);
                    SnackbarQueue* queue = q;
                    job->unregister = [queue, w] { std::erase(queue->waiters, w); };
                }
                void await_resume() const noexcept {}
            };

            // Hands the lock to the next live waiter, or frees it.
            void Unlock() {
                while (!waiters.empty()) {
                    auto w = waiters.front();
                    waiters.pop_front();
                    auto job = w->job.lock();
                    if (!job || job->cancelled) continue;
                    job->unregister = [this, w] { Unlock(); };   // cancelled before it runs: pass the lock on
                    std::weak_ptr<char> weak = alive;
                    rt::Dispatcher::Main().Post([w, weak] {
                        auto job = w->job.lock();
                        if (weak.expired() || !job || job->cancelled) return;
                        job->unregister = nullptr;
                        w->h.resume();
                    });
                    return;
                }
                locked = false;
            }
        };
    }

    namespace {
        class SnackbarDataImpl final : public SnackbarData {
        public:
            explicit SnackbarDataImpl(SnackbarVisuals v) : m_Visuals(std::move(v)) {}
            const SnackbarVisuals& Visuals() const override { return m_Visuals; }
            void PerformAction() override { Resolve(SnackbarResult::ActionPerformed); }
            void Dismiss() override { Resolve(SnackbarResult::Dismissed); }

            struct Wait {
                SnackbarDataImpl* d;
                bool await_ready() const noexcept { return d->m_Result.has_value(); }
                template<typename P> void await_suspend(std::coroutine_handle<P> h) {
                    auto job = rt::detail::JobOf(h);
                    d->m_Waiter = h;
                    d->m_Job = job;
                    SnackbarDataImpl* data = d;
                    job->unregister = [data] { data->m_Waiter = {}; data->m_Job.reset(); };
                }
                SnackbarResult await_resume() const { return *d->m_Result; }
            };
            Wait Result() { return { this }; }

        private:
            // continuation.isActive: only the first result counts, and only while someone waits.
            void Resolve(SnackbarResult r) {
                if (m_Result || !m_Waiter) return;
                auto job = m_Job.lock();
                if (!job || job->cancelled) return;
                m_Result = r;
                auto h = std::exchange(m_Waiter, {});
                m_Job.reset();
                job->unregister = nullptr;
                std::weak_ptr<rt::JobState> weak = job;
                rt::Dispatcher::Main().Post([h, weak] {
                    auto job = weak.lock();
                    if (job && !job->cancelled) h.resume();
                });
            }
            SnackbarVisuals m_Visuals;
            std::optional<SnackbarResult> m_Result;
            std::coroutine_handle<> m_Waiter;
            std::weak_ptr<rt::JobState> m_Job;
        };
    }

    SnackbarHostState::SnackbarHostState() : m_Queue(std::make_unique<internal::SnackbarQueue>()) {}
    SnackbarHostState::~SnackbarHostState() = default;

    rt::Task<SnackbarResult> SnackbarHostState::ShowSnackbar(std::string message, std::optional<std::string> actionLabel, bool withDismissAction,
                                                             std::optional<SnackbarDuration> duration) {
        const SnackbarDuration d = duration.value_or(actionLabel ? SnackbarDuration::Indefinite : SnackbarDuration::Short);
        co_return co_await ShowSnackbar(SnackbarVisuals{ std::move(message), std::move(actionLabel), withDismissAction, d });
    }

    rt::Task<SnackbarResult> SnackbarHostState::ShowSnackbar(SnackbarVisuals visuals) {
        co_await internal::SnackbarQueue::Lock{ m_Queue.get() };
        // finally { currentSnackbarData = null } and the lock's release, cancelled or not.
        struct Release {
            SnackbarHostState* s;
            ~Release() {
                s->m_Current.Set(nullptr);
                s->m_Queue->Unlock();
            }
        } release{ this };
        auto data = std::make_shared<SnackbarDataImpl>(std::move(visuals));
        m_Current.Set(data);
        co_return co_await data->Result();
    }

    SnackbarHostState& RememberSnackbarHostState() {
        return *rt::Composition::Current()->Remember<std::shared_ptr<SnackbarHostState>>([] { return std::make_shared<SnackbarHostState>(); });
    }

    std::int64_t internal::SnackbarDurationMillis(SnackbarDuration duration, bool hasAction, const ui::AccessibilityManager* manager) {
        const std::int64_t original = duration == SnackbarDuration::Indefinite ? INT64_MAX
                                    : duration == SnackbarDuration::Long       ? 10000
                                                                               : 4000;
        if (!manager) return original;
        return manager->CalculateRecommendedTimeoutMillis(original, true, true, hasAction);
    }

    // --- visuals --------------------------------------------------------------------------------

    namespace SnackbarDefaults {
        ShapePtr shape() { return Value(T::ContainerShape); }
        ui::Color color() { return Value(T::ContainerColor); }
        ui::Color contentColor() { return Value(T::SupportingTextColor); }
        ui::Color actionColor() { return Value(T::ActionLabelTextColor); }
        ui::Color actionContentColor() { return Value(T::ActionLabelTextColor); }
        ui::Color dismissActionContentColor() { return Value(T::IconColor); }
    }

    namespace {
        constexpr Dp ContainerMaxWidth{ 600 }, HeightToFirstLine{ 30 }, HorizontalSpacing{ 16 }, HorizontalSpacingButtonSide{ 8 },
            SeparateButtonExtraY{ 2 }, LegacySnackbarVerticalPadding{ 6 }, TextEndExtraSpacing{ 8 }, LongButtonVerticalOffset{ 12 },
            SnackbarVerticalPadding{ 14 }, ActionButtonBottomPadding{ 4 };

        struct Slots {
            std::function<void()> text, action, dismissAction;
            text::TextStyle actionTextStyle;
            ui::Color actionColor, dismissActionColor;
        };

        void ProvideAction(const Slots& s) {
            auto& c = *rt::Composition::Current();
            c.Provide(LocalContentColor, s.actionColor, [&] { c.Provide(LocalTextStyle(), s.actionTextStyle, s.action); });
        }
        void ProvideDismiss(const Slots& s) { rt::Composition::Current()->Provide(LocalContentColor, s.dismissActionColor, s.dismissAction); }

        PaddingValues Pad(Dp start, Dp top, Dp end, Dp bottom) { return PaddingValues{ start, top, end, bottom }; }

        void NewLineButtonSnackbar(const Slots& s) {
            Column(Padding(FillMax(SizeIn({}, Dp::Unspecified(), Dp::Unspecified(), ContainerMaxWidth, Dp::Unspecified()), Axis::Horizontal),
                           Pad(HorizontalSpacing, Dp(0), Dp(0), Dp(0))),
                   Arrangement::Top(), AlignmentH::Start(), [&] {
                       Box(Padding(Padding(FillMax({}, Axis::Horizontal), Dp(0), SnackbarVerticalPadding), Pad(Dp(0), Dp(0), HorizontalSpacing, Dp(0))),
                           Alignment::TopStart(), false, s.text);
                       const Dp end = s.dismissAction ? Dp(0) : HorizontalSpacingButtonSide;
                       Row(Padding(AlignInColumn({}, AlignmentH::End()), Pad(Dp(0), Dp(0), end, ActionButtonBottomPadding)), Arrangement::Start(),
                           AlignmentV::CenterVertically(), [&] {
                               ProvideAction(s);
                               if (s.dismissAction) ProvideDismiss(s);
                           });
                   });
        }

        void LegacyNewLineButtonSnackbar(const Slots& s) {
            Column(Padding(FillMax(SizeIn({}, Dp::Unspecified(), Dp::Unspecified(), ContainerMaxWidth, Dp::Unspecified()), Axis::Horizontal),
                           Pad(HorizontalSpacing, Dp(0), Dp(0), SeparateButtonExtraY)),
                   Arrangement::Top(), AlignmentH::Start(), [&] {
                       Box(Padding(PaddingFromBaseline({}, HeightToFirstLine, LongButtonVerticalOffset), Pad(Dp(0), Dp(0), HorizontalSpacingButtonSide, Dp(0))),
                           Alignment::TopStart(), false, s.text);
                       const Dp end = s.dismissAction ? Dp(0) : HorizontalSpacingButtonSide;
                       Box(Padding(AlignInColumn({}, AlignmentH::End()), Pad(Dp(0), Dp(0), end, Dp(0))), Alignment::TopStart(), false, [&] {
                           Row({}, [&] {
                               ProvideAction(s);
                               if (s.dismissAction) ProvideDismiss(s);
                           });
                       });
                   });
        }

        // The text, then the action, then the dismiss action (Compose finds them by layoutId).
        void OneRowContent(const Slots& s, Dp textVerticalPadding) {
            Box(Padding({}, Dp(0), textVerticalPadding), Alignment::TopStart(), false, s.text);
            if (s.action) Box({}, Alignment::TopStart(), false, [&] { ProvideAction(s); });
            if (s.dismissAction) Box({}, Alignment::TopStart(), false, [&] { ProvideDismiss(s); });
        }

        void LegacyOneRowSnackbar(const Slots& s) {
            const bool hasAction = bool(s.action), hasDismiss = bool(s.dismissAction);
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([hasAction, hasDismiss](ui::MeasureScope& m, std::span<ui::Measurable* const> children,
                                                                                            const ui::Constraints& cs) {
                const int containerWidth = std::min(cs.maxWidth, m.RoundToPx(ContainerMaxWidth));
                std::size_t i = 1;
                ui::Placeable* action = hasAction ? &children[i++]->Measure(cs) : nullptr;
                ui::Placeable* dismiss = hasDismiss ? &children[i++]->Measure(cs) : nullptr;
                const int actionWidth = action ? action->Width() : 0, actionHeight = action ? action->Height() : 0;
                const int dismissWidth = dismiss ? dismiss->Width() : 0, dismissHeight = dismiss ? dismiss->Height() : 0;
                const int extraSpacing = dismissWidth == 0 ? m.RoundToPx(TextEndExtraSpacing) : 0;
                const int textMaxWidth = std::max(containerWidth - actionWidth - dismissWidth - extraSpacing, cs.minWidth);
                ui::Constraints textConstraints = cs;
                textConstraints.minHeight = 0;
                textConstraints.maxWidth = textMaxWidth;
                textConstraints.minWidth = std::min(textConstraints.minWidth, textMaxWidth);
                ui::Placeable& text = children[0]->Measure(textConstraints);

                const int first = text.Get(ui::FirstBaseline), last = text.Get(ui::LastBaseline);
                const bool hasText = first != ui::AlignmentLine::Unspecified && last != ui::AlignmentLine::Unspecified;
                const bool oneLine = first == last || !hasText;
                const int dismissX = containerWidth - dismissWidth;
                const int actionX = dismissX - actionWidth;
                int textY, containerHeight, actionY;
                if (oneLine) {
                    containerHeight = std::max(m.RoundToPx(T::SingleLineContainerHeight), std::max(actionHeight, dismissHeight));
                    textY = (containerHeight - text.Height()) / 2;
                    actionY = 0;
                    if (action) {
                        const int b = action->Get(ui::FirstBaseline);
                        actionY = b != ui::AlignmentLine::Unspecified ? textY + first - b : 0;
                    }
                } else {
                    textY = m.RoundToPx(HeightToFirstLine) - first;
                    containerHeight = std::max(m.RoundToPx(T::TwoLinesContainerHeight), textY + text.Height());
                    actionY = action ? (containerHeight - action->Height()) / 2 : 0;
                }
                const int dismissY = dismiss ? (containerHeight - dismiss->Height()) / 2 : 0;
                return m.Layout(containerWidth, containerHeight, [&text, action, dismiss, textY, actionX, actionY, dismissX, dismissY](ui::PlacementScope& p) {
                    p.PlaceRelative(text, 0, textY);
                    if (action) p.PlaceRelative(*action, actionX, actionY);
                    if (dismiss) p.PlaceRelative(*dismiss, dismissX, dismissY);
                });
            });
            Layout(Padding({}, Pad(HorizontalSpacing, Dp(0), hasDismiss ? Dp(0) : HorizontalSpacingButtonSide, Dp(0))), policy,
                   [s] { OneRowContent(s, LegacySnackbarVerticalPadding); });
        }

        void OneRowSnackbar(const Slots& s) {
            const bool hasAction = bool(s.action), hasDismiss = bool(s.dismissAction);
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([hasAction, hasDismiss](ui::MeasureScope& m, std::span<ui::Measurable* const> children,
                                                                                            const ui::Constraints& cs) {
                const int minContainerHeight = m.RoundToPx(T::SingleLineContainerHeight);
                const int containerWidth = std::min(cs.maxWidth, m.RoundToPx(ContainerMaxWidth));
                std::size_t i = 1;
                ui::Placeable* action = hasAction ? &children[i++]->Measure(cs) : nullptr;
                ui::Placeable* dismiss = hasDismiss ? &children[i++]->Measure(cs) : nullptr;
                const int actionWidth = action ? action->Width() : 0, dismissWidth = dismiss ? dismiss->Width() : 0;
                const int extraSpacing = dismissWidth == 0 ? m.RoundToPx(TextEndExtraSpacing) : 0;
                const int textMaxWidth = std::max(containerWidth - actionWidth - dismissWidth - extraSpacing, cs.minWidth);
                ui::Constraints textConstraints = cs;
                textConstraints.minHeight = 0;
                textConstraints.maxWidth = textMaxWidth;
                textConstraints.minWidth = std::min(textConstraints.minWidth, textMaxWidth);
                ui::Placeable& text = children[0]->Measure(textConstraints);
                const int containerHeight = std::max({ minContainerHeight, text.Height(), action ? action->Height() : 0, dismiss ? dismiss->Height() : 0 });
                const int dismissX = containerWidth - dismissWidth;
                const int actionX = dismissX - actionWidth;
                return m.Layout(containerWidth, containerHeight, [&text, action, dismiss, containerHeight, actionX, dismissX](ui::PlacementScope& p) {
                    p.PlaceRelative(text, 0, (containerHeight - text.Height()) / 2);
                    if (action) p.PlaceRelative(*action, actionX, (containerHeight - action->Height()) / 2);
                    if (dismiss) p.PlaceRelative(*dismiss, dismissX, (containerHeight - dismiss->Height()) / 2);
                });
            });
            Layout(Padding({}, Pad(HorizontalSpacing, Dp(0), hasDismiss ? Dp(0) : TextEndExtraSpacing, Dp(0))), policy,
                   [s] { OneRowContent(s, SnackbarVerticalPadding); });
        }
    }

    void Snackbar(const ui::Modifier& modifier, const SnackbarOptions& o, std::function<void()> content) {
        SurfaceOptions so;
        so.shape = o.shape ? o.shape : SnackbarDefaults::shape();
        so.color = o.containerColor.value_or(SnackbarDefaults::color());
        so.contentColor = o.contentColor.value_or(SnackbarDefaults::contentColor());
        so.shadowElevation = T::ContainerElevation;
        Slots s{ std::move(content), o.action, o.dismissAction, Value(T::ActionLabelTextFont),
                 o.actionContentColor.value_or(SnackbarDefaults::actionContentColor()),
                 o.dismissActionContentColor.value_or(SnackbarDefaults::dismissActionContentColor()) };
        const bool newLine = o.actionOnNewLine;
        Surface(modifier, so, [s, newLine] {
            rt::Composition::Current()->Provide(LocalTextStyle(), Value(T::SupportingTextFont), [&] {
                const bool fix = ComposeMaterial3Flags::isSnackbarStylingFixEnabled;
                if (newLine && s.action) {
                    if (fix) NewLineButtonSnackbar(s);
                    else LegacyNewLineButtonSnackbar(s);
                } else {
                    if (fix) OneRowSnackbar(s);
                    else LegacyOneRowSnackbar(s);
                }
            });
        });
    }

    void Snackbar(std::shared_ptr<SnackbarData> data, const ui::Modifier& modifier, const SnackbarDataOptions& o) {
        SnackbarOptions so;
        const auto& visuals = data->Visuals();
        if (visuals.actionLabel) {
            const ui::Color actionColor = o.actionColor.value_or(SnackbarDefaults::actionColor());
            so.action = [data, label = *visuals.actionLabel, actionColor] {
                ButtonOptions bo;
                bo.colors = ButtonDefaults::textButtonColors().Copy(ui::Color::Unspecified(), actionColor);
                TextButton([data] { data->PerformAction(); }, {}, bo, [label] { Text(label); });
            };
        }
        if (visuals.withDismissAction) {
            so.dismissAction = [data] {
                const std::string description = GetString(Strings::SnackbarDismiss);
                TooltipBox(TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition::Above),
                           [description](const TooltipScope& scope) {
                               internal::PlainTooltipInternal(scope, description, {}, {}, [description] { Text(description); });
                           },
                           RememberTooltipState(), {}, {}, [data, description] {
                               IconButton([data] { data->Dismiss(); }, {}, {},
                                          [description] { Icon(internal::Icons::Filled::Close(), description); });
                           });
            };
        }
        so.actionOnNewLine = o.actionOnNewLine;
        so.shape = o.shape;
        so.containerColor = o.containerColor;
        so.contentColor = o.contentColor;
        so.actionContentColor = o.actionContentColor;
        so.dismissActionContentColor = o.dismissActionContentColor;
        Snackbar(Padding(modifier, Dp(12)), so, [message = visuals.message] { Text(message); });
    }

    // --- the host -------------------------------------------------------------------------------

    namespace {
        // FadeInFadeOutState: the snackbars on screen, the current one last.
        struct FadeState {
            bool initialized = false;   // `current` starts as something no data equals
            std::shared_ptr<SnackbarData> current;
            std::vector<std::shared_ptr<SnackbarData>> items;
            rt::State<int> version{ 0 };   // currentRecomposeScope.invalidate()
        };

        // animatedOpacity / animatedScale: from where an item enters or leaves, towards visible.
        std::shared_ptr<anim::Animatable> Animated(bool visible, float hidden, anim::SpecPtr spec, std::function<void()> onFinish, int key) {
            auto& c = *rt::Composition::Current();
            auto value = c.Remember<std::shared_ptr<anim::Animatable>>([&] { return std::make_shared<anim::Animatable>(visible ? hidden : 1.0f); },
                                                                      {}, rt::Key(key));
            c.LaunchedEffect(rt::Keys(visible), [value, visible, hidden, spec, onFinish]() -> rt::Task<> {
                return [](std::shared_ptr<anim::Animatable> a, float target, anim::SpecPtr spec, std::function<void()> done) -> rt::Task<> {
                    co_await a->AnimateTo(target, spec);
                    if (done) done();
                }(value, visible ? 1.0f : hidden, spec, onFinish);
            }, rt::Key(key));
            return value;
        }

        void FadeInFadeOutWithScale(const std::shared_ptr<SnackbarData>& current, const ui::Modifier& modifier,
                                    const std::function<void(std::shared_ptr<SnackbarData>)>& content) {
            auto& c = *rt::Composition::Current();
            const std::string paneTitle = GetString(Strings::SnackbarPaneTitle);
            auto state = c.Remember<std::shared_ptr<FadeState>>([] { return std::make_shared<FadeState>(); });
            if (!state->initialized || current != state->current) {
                state->initialized = true;
                state->current = current;
                if (current && std::find(state->items.begin(), state->items.end(), current) == state->items.end()) state->items.push_back(current);
            }
            Box(modifier, Alignment::TopStart(), false, [state, content, paneTitle] {
                (void)state->version.Get();
                const auto items = state->items;
                for (const auto& item : items) {
                    rt::Composition::Current()->Child([state, item, content, paneTitle] {
                        const bool visible = item == state->current;
                        std::weak_ptr<FadeState> weak = state;
                        auto opacity = Animated(visible, 0.0f, Value(tokens::MotionSchemeKeyTokens::FastEffects), [weak, item] {
                            auto s = weak.lock();
                            if (!s || item == s->current) return;
                            // Leave only the current one.
                            std::erase(s->items, item);
                            s->version.Set(s->version.Peek() + 1);
                        }, 1);
                        auto scale = Animated(visible, 0.8f, Value(tokens::MotionSchemeKeyTokens::FastSpatial), {}, 2);
                        Modifier m = GraphicsLayer({}, [opacity, scale](GraphicsLayerParams& p) {
                            p.scaleX = p.scaleY = scale->Float();
                            p.alpha = opacity->Float();
                        });
                        m = Semantics(std::move(m), [visible, item, paneTitle](ui::SemanticsConfiguration& sc) {
                            if (visible) sc.liveRegion = ui::LiveRegionMode::Polite;
                            sc.dismiss = [item] { item->Dismiss(); return true; };
                            sc.paneTitle = paneTitle;
                        });
                        Box(m, Alignment::TopStart(), false, [content, item] { content(item); });
                    }, rt::Key(std::int64_t(reinterpret_cast<std::uintptr_t>(item.get()))));
                }
            });
        }

        rt::Task<> DismissAfter(std::shared_ptr<SnackbarData> data, std::int64_t millis) {
            // delay(Long.MAX_VALUE) never ends.
            constexpr std::int64_t kDay = 24LL * 3600 * 1000;
            while (millis > kDay) {
                co_await rt::Delay(kDay);
                if (millis != INT64_MAX) millis -= kDay;
            }
            co_await rt::Delay(millis);
            data->Dismiss();
        }
    }

    void SnackbarHost(SnackbarHostState& hostState, const ui::Modifier& modifier, std::function<void(std::shared_ptr<SnackbarData>)> snackbar) {
        auto& c = *rt::Composition::Current();
        const auto current = hostState.CurrentSnackbarData();
        const auto manager = c.Read(ui::LocalAccessibilityManager);
        const std::int64_t millis = current ? internal::SnackbarDurationMillis(current->Visuals().duration, current->Visuals().actionLabel.has_value(), manager.get()) : 0;
        c.LaunchedEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(current.get())), [current, millis]() -> rt::Task<> {
            if (!current) return []() -> rt::Task<> { co_return; }();
            return DismissAfter(current, millis);
        });
        if (!snackbar) snackbar = [](std::shared_ptr<SnackbarData> data) { Snackbar(std::move(data)); };
        FadeInFadeOutWithScale(current, modifier, snackbar);
    }

}
