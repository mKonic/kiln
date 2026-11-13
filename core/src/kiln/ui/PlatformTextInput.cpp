#include "kiln/ui/PlatformTextInput.h"
#include <kiln/Function.h>

#include "kiln/ui/LayoutNode.h"

namespace kiln::ui {

    namespace {
        // Suspends until the task is cancelled, then stops the input method it started.
        struct AwaitCancellation {
            Owner* owner;
            TextInputSession* request;
            bool await_ready() const noexcept { return false; }
            template<typename P>
            void await_suspend(std::coroutine_handle<P> h) {
                if (auto job = h.promise().job) {
                    job->unregister = [owner = owner, request = request] { owner->StopTextInput(*request); };
                }
            }
            void await_resume() const noexcept {}
        };

        // The window's own handler: the host's input method.
        class RootSession final : public PlatformTextInputSession {
        public:
            explicit RootSession(Owner& owner) : m_Owner(owner) {}
            rt::Task<> StartInputMethod(PlatformTextInputMethodRequest& request) override {
                m_Owner.StartTextInput(request);
                co_await AwaitCancellation{ &m_Owner, &request };
            }
        private:
            Owner& m_Owner;
        };

        // An interceptor in front of the next handler.
        class InterceptedSession final : public PlatformTextInputSession {
        public:
            InterceptedSession(std::shared_ptr<PlatformTextInputInterceptor> i, PlatformTextInputSession& next) : m_Interceptor(std::move(i)), m_Next(next) {}
            rt::Task<> StartInputMethod(PlatformTextInputMethodRequest& request) override {
                return m_Interceptor->InterceptStartInputMethod(request, m_Next);
            }
        private:
            std::shared_ptr<PlatformTextInputInterceptor> m_Interceptor;
            PlatformTextInputSession& m_Next;
        };

        // The task of the window's current session, cancelled when another starts.
        struct CurrentSession {
            std::weak_ptr<rt::JobState> job;
        };
        CurrentSession& CurrentFor(Owner& owner) {
            auto& slot = owner.Service("PlatformTextInputSession");
            if (!slot) slot = std::make_shared<CurrentSession>();
            return *static_cast<CurrentSession*>(slot.get());
        }

        struct CurrentJob {
            std::shared_ptr<rt::JobState> job;
            bool await_ready() const noexcept { return false; }
            template<typename P> bool await_suspend(std::coroutine_handle<P> h) { job = h.promise().job; return false; }
            std::shared_ptr<rt::JobState> await_resume() { return std::move(job); }
        };
    }

    void InterceptPlatformTextInput(std::shared_ptr<PlatformTextInputInterceptor> interceptor, kiln::Function<void()> content, rt::Key key) {
        rt::Composition* c = rt::Composition::Current();
        auto chain = c->Read(LocalPlatformTextInputInterceptors);
        chain.push_back(std::move(interceptor));
        c->Provide(LocalPlatformTextInputInterceptors, std::move(chain), std::move(content), key);
    }

    rt::Task<> EstablishTextInputSession(ModifierNode& node, kiln::Function<rt::Task<>(PlatformTextInputSessionScope&)> block) {
        LayoutNode* layout = node.Owner();
        Owner* owner = layout ? layout->GetOwner() : nullptr;
        if (!owner) co_return;
        // The interceptors where the node was composed, innermost first in front of the root.
        const auto chain = CurrentValueOf(node, LocalPlatformTextInputInterceptors);
        // A session is the window's only one: the previous one's task is cancelled.
        CurrentSession& current = CurrentFor(*owner);
        std::shared_ptr<rt::JobState> mine = co_await CurrentJob{};
        if (auto previous = current.job.lock(); previous && previous != mine && !previous->cancelled) rt::Job(previous).Cancel();
        current.job = mine;
        RootSession root(*owner);
        std::vector<std::unique_ptr<InterceptedSession>> wrapped;
        PlatformTextInputSession* session = &root;
        for (const auto& interceptor : chain) {
            wrapped.push_back(std::make_unique<InterceptedSession>(interceptor, *session));
            session = wrapped.back().get();
        }
        co_await block(*session);
    }

}
