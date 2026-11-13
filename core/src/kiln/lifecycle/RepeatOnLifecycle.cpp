#include "kiln/lifecycle/RepeatOnLifecycle.h"
#include <kiln/Function.h>

#include "kiln/runtime/Channel.h"

namespace kiln::lifecycle {

    rt::Task<> RepeatOnLifecycle(Lifecycle& lifecycle, State state, kiln::Function<rt::Task<>()> block) {
        if (state == State::Initialized) throw std::invalid_argument("repeatOnLifecycle cannot start work with the INITIALIZED lifecycle state.");
        if (lifecycle.CurrentState() == State::Destroyed) co_return;

        struct Context {
            rt::Channel<bool> destroyed{ 1 };
            rt::Job launched;
            std::shared_ptr<LifecycleEventObserver> observer;
        };
        auto ctx = std::make_shared<Context>();
        const auto startWork = UpTo(state);
        const auto cancelWork = DownFrom(state);
        std::weak_ptr<Context> weak = ctx;
        ctx->observer = MakeEventObserver([weak, startWork, cancelWork, block](LifecycleOwner&, Event event) {
            auto c = weak.lock();
            if (!c) return;
            if (event == startWork) {
                c->launched = rt::Launch(rt::Dispatcher::Main(), block());
                return;
            }
            if (event == cancelWork) c->launched.Cancel();
            if (event == Event::OnDestroy) c->destroyed.TrySend(true);
        });
        // finally: cancel the work and stop observing, on return or when cancelled.
        struct Finally {
            Lifecycle* lifecycle;
            std::shared_ptr<Context> ctx;
            ~Finally() {
                ctx->launched.Cancel();
                lifecycle->RemoveObserver(ctx->observer);
            }
        } finally{ &lifecycle, ctx };
        lifecycle.AddObserver(ctx->observer);
        co_await ctx->destroyed.Next();
    }

    namespace detail {
        rt::Task<> WithStateAtLeastUnchecked(Lifecycle& lifecycle, State state, kiln::Function<void()> block) {
            if (lifecycle.CurrentState() == State::Destroyed) throw LifecycleDestroyedException();
            if (lifecycle.CurrentState() >= state) {
                block();
                co_return;
            }
            struct Context {
                rt::Channel<bool> done{ 1 };
                bool destroyed = false;
                std::exception_ptr error;
                std::shared_ptr<LifecycleEventObserver> observer;
            };
            auto ctx = std::make_shared<Context>();
            std::weak_ptr<Context> weak = ctx;
            Lifecycle* l = &lifecycle;
            const auto reach = UpTo(state);
            ctx->observer = MakeEventObserver([weak, l, reach, block](LifecycleOwner&, Event event) {
                auto c = weak.lock();
                if (!c) return;
                if (event == reach) {
                    l->RemoveObserver(c->observer);
                    try { block(); } catch (...) { c->error = std::current_exception(); }
                    c->done.TrySend(true);
                } else if (event == Event::OnDestroy) {
                    l->RemoveObserver(c->observer);
                    c->destroyed = true;
                    c->done.TrySend(true);
                }
            });
            struct Finally {
                Lifecycle* lifecycle;
                std::shared_ptr<Context> ctx;
                ~Finally() { lifecycle->RemoveObserver(ctx->observer); }
            } finally{ &lifecycle, ctx };
            lifecycle.AddObserver(ctx->observer);
            co_await ctx->done.Next();
            if (ctx->destroyed) throw LifecycleDestroyedException();
            if (ctx->error) std::rethrow_exception(ctx->error);
        }
    }

}
