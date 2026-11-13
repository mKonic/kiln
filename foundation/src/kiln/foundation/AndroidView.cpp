#include "kiln/foundation/AndroidView.h"
#include <kiln/Function.h>

#include "kiln/foundation/AndroidExternalSurface.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/BringIntoView.h"

#if defined(__ANDROID__)
#include <jni.h>
#endif

#include <cmath>
#include <memory>

namespace kiln::foundation {

    namespace {

        class InteropNode;

        // What one call of AndroidView passes on: remembered at the call site (a reused element
        // gets new ones), while the View itself stays with the element's node.
        struct Callbacks {
            ui::InteropViewFactory factory;
            AndroidViewOptions options;
            std::shared_ptr<NestedScrollDispatcher> dispatcher = std::make_shared<NestedScrollDispatcher>();
            InteropNode* node = nullptr;   // the node holding the View, while attached
        };
        using CallbacksPtr = std::shared_ptr<Callbacks>;

        // composeToViewOffset: whole pixels, rounded away from zero, in the View's sign.
        int ToView(float v) { return -int(v >= 0 ? std::ceil(v) : std::floor(v)); }

        // The View's holder on kiln's side (AndroidViewHolder + pointerInteropFilter): makes and
        // keeps the View while attached, places it, hands it touches, and runs the callbacks.
        class InteropNode final : public ui::ModifierNode, public ui::PointerInputModifier, public ui::GlobalPositionAwareModifier,
                                  public ui::LayoutAwareModifier, public ui::InteropViewClient, public rt::Subscriber {
        public:
            CallbacksPtr params;
            explicit InteropNode(CallbacksPtr p) : params(std::move(p)) { params->node = this; }
            ~InteropNode() override {
                if (params && params->node == this) params->node = nullptr;
                if (m_Owner && m_Id) { m_Kept = false; Release(); }   // kept for reuse, then dropped
            }

            ui::PointerInputModifier* AsPointerInput() override { return this; }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            ui::LayoutAwareModifier* AsLayoutAware() override { return this; }

            void OnParamsChanged() { params->node = this; }

            void OnAttach() override {
                ui::Owner* owner = Owner() ? Owner()->GetOwner() : nullptr;
                if (!owner) return;
                if (m_Owner == owner && m_Id) { m_Kept = false; RunUpdate(); return; }   // kept through reuse
                m_Owner = owner;
                m_Id = owner->AddInteropView(this);
                const ui::InteropViewHost& host = owner->InteropViews();
                m_Created = host.create && host.create(m_Id, params->factory);
                RunUpdate();
            }
            void OnDetach() override {
                if (!m_Owner) return;
                if (m_Kept) {   // kept for reuse: off screen until its new content places it
                    if (const auto& place = m_Owner->InteropViews().place) place(m_Id, {}, false);
                    return;
                }
                Release();
            }
            // onReset: the element is kept for reuse; with an onReset the View stays and resets.
            void OnReset() override {
                if (!m_Created || !params->options.onReset) return;
                m_Kept = true;
                With(params->options.onReset);
            }
            // Content asks: measure the View, and size questions.
            bool Created() const { return m_Owner && m_Created; }
            const ui::InteropViewHost* Host() const { return m_Owner ? &m_Owner->InteropViews() : nullptr; }
            int Id() const { return m_Id; }

            // update: under observation, so state it reads runs it again (onCommitAffectingUpdate).
            void RunUpdate() {
                if (!Created() || !params->options.update) return;
                rt::Observation::Track(*this, [&] { With(params->options.update); });
            }
            void OnStateChanged() override {
                std::weak_ptr<const char> alive = m_Alive;
                rt::Dispatcher::Main().Post([this, alive] { if (!alive.expired()) RunUpdate(); });
            }

            void SetTarget(ui::PlatformSurface surface, int width, int height) {
                if (const auto* host = Host(); host && host->target && m_Created) host->target(m_Id, surface, width, height);
            }

            void OnGloballyPositioned() override {
                const ui::Rect r = ui::RequireLayoutCoordinates(*this).BoundsInRoot();
                Place({ int(std::lround(r.left)), int(std::lround(r.top)), int(std::lround(r.right)), int(std::lround(r.bottom)) }, true);
            }
            void OnUnplaced() override { Place({}, false); }

            // pointerInteropFilter: the View sees the gesture from its down until it declines it or
            // something else consumes a change; while it disallows intercepting, scrolling
            // parents leave the moves to it.
            void OnPointerEvent(ui::PointerEvent& event, ui::PointerEventPass pass, ui::IntSize) override {
                auto& changes = event.changes;
                if (changes.empty()) return;
                const bool panStartOrEnd = event.type == ui::PointerEventType::PanStart || event.type == ui::PointerEventType::PanEnd;
                const bool isMove = !panStartOrEnd && std::all_of(changes.begin(), changes.end(), [](const ui::PointerInputChange& c) {
                    return !c.ChangedToDownIgnoreConsumed() && !c.ChangedToUpIgnoreConsumed();
                });
                const bool unconsumedMove = isMove && std::none_of(changes.begin(), changes.end(), [](const ui::PointerInputChange& c) { return c.IsConsumed(); });
                const bool duringInitial = m_Disallow || panStartOrEnd || unconsumedMove ||
                    std::any_of(changes.begin(), changes.end(), [](const ui::PointerInputChange& c) {
                        return c.ChangedToDownIgnoreConsumed() || c.ChangedToUpIgnoreConsumed();
                    });
                const void* identity = changes.front().consumed.get();   // one per dispatch, shared by its passes
                if (m_State != State::NotDispatching) {
                    if (pass == ui::PointerEventPass::Initial && duringInitial) {
                        m_LastInitial = identity;
                        Dispatch(event, !isMove || m_Disallow);
                    }
                    if (pass == ui::PointerEventPass::Main && isMove && identity == m_LastInitial && m_Disallow)
                        for (auto& c : changes) c.Consume();
                    if (pass == ui::PointerEventPass::Final && !duringInitial && identity != m_LastInitial) Dispatch(event, true);
                }
                if (pass == ui::PointerEventPass::Final) {
                    if (std::all_of(changes.begin(), changes.end(), [](const ui::PointerInputChange& c) { return c.ChangedToUpIgnoreConsumed(); }) ||
                        event.type == ui::PointerEventType::PanEnd) {
                        ResetPointer();
                    } else if (identity == m_LastInitial && isMove) {
                        if (std::any_of(changes.begin(), changes.end(), [](const ui::PointerInputChange& c) { return c.IsConsumed(); }) && !m_Disallow)
                            StopDispatching();
                        else
                            for (auto& c : changes) c.Consume();
                    }
                }
            }
            void OnCancelPointerInput() override {
                if (m_State == State::Dispatching) Touch(true);
                ResetPointer();
            }

            // InteropViewClient
            void OnRequestRemeasure() override { if (Owner()) Owner()->RequestRemeasure(); }
            void OnRequestDisallowInterceptTouchEvent(bool disallow) override { m_Disallow = disallow; }
            IntOffset OnNestedPreScroll(int dx, int dy, bool touch) override {
                const Offset c = params->dispatcher->DispatchPreScroll({ -float(dx), -float(dy) }, Source(touch));
                return { ToView(c.x), ToView(c.y) };
            }
            IntOffset OnNestedScroll(int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed, bool touch) override {
                const Offset c = params->dispatcher->DispatchPostScroll({ -float(dxConsumed), -float(dyConsumed) },
                                                                        { -float(dxUnconsumed), -float(dyUnconsumed) }, Source(touch));
                return { ToView(c.x), ToView(c.y) };
            }
            void OnNestedPreFling(float vx, float vy) override {
                m_Fling = rt::Launch(rt::Dispatcher::Main(), PreFling(params->dispatcher, { -vx, -vy }));
            }
            void OnNestedFling(float vx, float vy, bool consumed) override {
                const Offset v{ -vx, -vy };
                m_Fling = rt::Launch(rt::Dispatcher::Main(), PostFling(params->dispatcher, consumed ? v : Offset{}, consumed ? Offset{} : v));
            }
            const ui::LayoutNode* Node() const override { return Owner(); }
            void OnRequestRectangleOnScreen(ui::Rect rect) override {
                if (Owner()) ui::BringIntoView(*Owner(), rect, this);
            }

        private:
            enum class State : std::uint8_t { Unknown, Dispatching, NotDispatching };

            void With(const ui::InteropViewBlock& block) {
                if (const auto* host = Host(); host && host->with && m_Created && block) host->with(m_Id, block);
            }
            void Release() {
                if (m_Created) With(params->options.onRelease);
                Unsubscribe();
                m_Owner->RemoveInteropView(m_Id);
                m_Owner = nullptr;
                m_Id = 0;
                m_Created = false;
                m_Kept = false;
            }
            void Place(ui::IntRect bounds, bool visible) {
                if (const auto* host = Host(); host && host->place && m_Created) host->place(m_Id, bounds, visible);
            }
            bool Touch(bool cancel) {
                const auto* host = Host();
                if (!host || !host->touch || !m_Created) return false;
                return host->touch(m_Id, ui::RequireLayoutCoordinates(*this).PositionInRoot(), cancel);
            }
            void Dispatch(ui::PointerEvent& event, bool consume) {
                auto& changes = event.changes;
                if (std::any_of(changes.begin(), changes.end(), [](const ui::PointerInputChange& c) { return c.IsConsumed(); })) {
                    StopDispatching();
                    return;
                }
                const bool down = std::any_of(changes.begin(), changes.end(), [](const ui::PointerInputChange& c) {
                    return c.ChangedToDownIgnoreConsumed();
                }) && std::count_if(changes.begin(), changes.end(), [](const ui::PointerInputChange& c) { return c.pressed; }) == 1;
                const bool took = Touch(false);
                if (down) m_State = took ? State::Dispatching : State::NotDispatching;
                if (m_State == State::Dispatching && consume) for (auto& c : changes) c.Consume();
            }
            void StopDispatching() {
                if (m_State == State::Dispatching) Touch(true);
                m_State = State::NotDispatching;
            }
            void ResetPointer() {
                m_State = State::Unknown;
                m_Disallow = false;
                m_LastInitial = nullptr;
            }
            static ui::NestedScrollSource Source(bool touch) { return touch ? ui::NestedScrollSource::UserInput : ui::NestedScrollSource::SideEffect; }
            static rt::Task<> PreFling(std::shared_ptr<NestedScrollDispatcher> d, Offset v) { co_await d->DispatchPreFling(v); }
            static rt::Task<> PostFling(std::shared_ptr<NestedScrollDispatcher> d, Offset consumed, Offset available) {
                co_await d->DispatchPostFling(consumed, available);
            }

            ui::Owner* m_Owner = nullptr;
            int m_Id = 0;
            bool m_Created = false;
            bool m_Kept = false;
            State m_State = State::Unknown;
            bool m_Disallow = false;
            const void* m_LastInitial = nullptr;
            rt::Job m_Fling;
            std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
        };

        // The View measures itself (AndroidViewHolder's measure policy): against the
        // constraints, by its layout params, in the host.
        class InteropPolicy final : public ui::MeasurePolicy {
        public:
            explicit InteropPolicy(CallbacksPtr c) : m_Callbacks(std::move(c)) {}
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const>, const ui::Constraints& c) override {
                InteropNode* n = m_Callbacks->node;
                if (!n || !n->Created() || !n->Host()->measure) return s.Layout(c.minWidth, c.minHeight);
                const ui::IntSize size = n->Host()->measure(n->Id(), c);
                return s.Layout(c.ConstrainWidth(size.width), c.ConstrainHeight(size.height));
            }
            int MinIntrinsicWidth(ui::MeasureScope&, std::span<ui::Measurable* const>, int height) override { return Intrinsic(-1, height).width; }
            int MaxIntrinsicWidth(ui::MeasureScope&, std::span<ui::Measurable* const>, int height) override { return Intrinsic(-1, height).width; }
            int MinIntrinsicHeight(ui::MeasureScope&, std::span<ui::Measurable* const>, int width) override { return Intrinsic(width, -1).height; }
            int MaxIntrinsicHeight(ui::MeasureScope&, std::span<ui::Measurable* const>, int width) override { return Intrinsic(width, -1).height; }
        private:
            ui::IntSize Intrinsic(int width, int height) const {
                InteropNode* n = m_Callbacks->node;
                if (!n || !n->Created() || !n->Host()->intrinsic) return {};
                return n->Host()->intrinsic(n->Id(), width, height);
            }
            CallbacksPtr m_Callbacks;
        };

        // NoOpScrollConnection: the element takes part in nested scrolling only to dispatch.
        class NoOpConnection final : public ui::NestedScrollConnection {};
    }

    void AndroidView(ui::InteropViewFactory factory, const Modifier& modifier, AndroidViewOptions options, rt::Key key) {
        rt::Composition& c = *rt::Composition::Current();
        auto& callbacks = c.Remember<CallbacksPtr>([] { return std::make_shared<Callbacks>(); }, {}, key);
        callbacks->factory = std::move(factory);
        callbacks->options = std::move(options);
        auto& policy = c.Remember<std::shared_ptr<ui::MeasurePolicy>>([&] { return std::make_shared<InteropPolicy>(callbacks); }, {}, key);
        static const auto noOp = std::make_shared<NoOpConnection>();
        CallbacksPtr cb = callbacks;
        Modifier core = NestedScroll(Modifier{}, noOp, cb->dispatcher);
        core = Semantics(std::move(core), [](ui::SemanticsConfiguration&) {}, true);
        core = detail::EmbeddedSurface(core.With<InteropNode>(cb), false, [cb](AndroidExternalSurfaceScope& scope) {
            scope.OnSurface([cb](SurfaceCoroutineScope& s, Surface surface, int width, int height) -> rt::Task<> {
                if (cb->node) cb->node->SetTarget(surface, width, height);
                s.OnChanged([cb](Surface changed, int w, int h) { if (cb->node) cb->node->SetTarget(changed, w, h); });
                s.OnDestroyed([cb](Surface) { if (cb->node) cb->node->SetTarget({}, 0, 0); });
                co_return;
            });
        }, key);
        // The View is updated after each recomposition too (its update block is a new one).
        c.SideEffect([cb] { if (cb->node) cb->node->RunUpdate(); });
        Layout(modifier.Then(core), policy, {}, key);
    }

    ui::InteropViewFactory AndroidViewClass(std::string className) {
        return [className](void* envPtr, void* context) -> void* {
#if defined(__ANDROID__)
            // Through the Context's class loader, so the app's own View classes load too.
            auto* env = static_cast<JNIEnv*>(envPtr);
            auto* ctx = static_cast<jobject>(context);
            jclass contextClass = env->GetObjectClass(ctx);
            jmethodID getLoader = env->GetMethodID(contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
            jobject loader = env->CallObjectMethod(ctx, getLoader);
            jclass loaderClass = env->FindClass("java/lang/ClassLoader");
            jmethodID loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
            jstring name = env->NewStringUTF(className.c_str());
            auto cls = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, name));
            jobject view = nullptr;
            if (env->ExceptionCheck() || !cls) {
                env->ExceptionClear();
            } else if (jmethodID ctor = env->GetMethodID(cls, "<init>", "(Landroid/content/Context;)V")) {
                view = env->NewObject(cls, ctor, ctx);
                if (env->ExceptionCheck()) { env->ExceptionClear(); view = nullptr; }
            } else {
                env->ExceptionClear();
            }
            for (jobject ref : { static_cast<jobject>(contextClass), loader, static_cast<jobject>(loaderClass), static_cast<jobject>(name), static_cast<jobject>(cls) })
                if (ref) env->DeleteLocalRef(ref);
            return view;
#else
            (void)envPtr;
            (void)context;
            return nullptr;
#endif
        };
    }

}
