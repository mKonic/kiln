#pragma once

// Flows (kotlinx.coroutines flow/: Flow.kt, Builders.kt, StateFlow.kt, SharedFlow.kt,
// operators/*, terminal/*, Share.kt) on kiln's main-thread coroutines.
//
//   Flow<int> ticks = Flow<int>::Create([](FlowCollector<int> emit) -> Task<> {
//       for (int i = 0; ; ++i) { co_await emit(i); co_await Delay(1000); }
//   });
//   Job job = Launch(Dispatcher::Main(), Map(ticks, [](int i) { return i * 2; }).Collect([](const int& v) { ... }));
//
// A Flow is cold: each Collect runs its body again. Emitting suspends until the collector has
// taken the value (a co_await of the collector's task), as Kotlin's emit does. Collect with a
// plain function (the contract CollectAsStateWithLifecycle and SnapshotFlow share) handles each
// value at once. Operators that need concurrency (Combine, Zip, FlatMapLatest, Debounce, Buffer)
// run their upstream collections as child jobs owned by the collecting frame, so cancelling the
// collector cancels them. MutableStateFlow and MutableSharedFlow are hot; StateIn and ShareIn
// share an upstream through a CoroutineScope.
//
// Everything runs on the main dispatcher; blocking work goes through WithContext (kiln has no
// flowOn: a flow's body runs where it is collected).

#include "kiln/runtime/Channel.h"
#include "kiln/runtime/CoroutineScope.h"
#include "kiln/runtime/Task.h"

#include <kiln/Function.h>

#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <tuple>
#include <variant>
#include <utility>
#include <vector>

namespace kiln::rt {

    template<typename T>
    using FlowCollector = kiln::Function<Task<>(T)>;

    namespace detail {
        // AbortFlowException: a downstream that has what it needs (take, first) stops upstream.
        struct AbortFlow {
            const void* owner;
        };
        inline Task<> Ready() { co_return; }
    }

    template<typename T>
    class Flow {
    public:
        using value_type = T;
        using Body = kiln::Function<Task<>(FlowCollector<T>)>;

        Flow() : Flow(Body([](FlowCollector<T>) -> Task<> { co_return; })) {}
        explicit Flow(Body body) : m_Body(std::make_shared<const Body>(std::move(body))) {}
        // flow { ... }
        static Flow Create(Body body) { return Flow(std::move(body)); }

        // collect { }: runs the body, awaiting `collector` for each value. The task holds the
        // body (and what it captured), so the Flow object may go before the task runs.
        Task<> Collect(FlowCollector<T> collector) const { return Run(m_Body, std::move(collector)); }
        // collect with a plain function: each value handled at once.
        Task<> Collect(kiln::Function<void(const T&)> collector) const {
            auto fn = std::make_shared<kiln::Function<void(const T&)>>(std::move(collector));
            return Run(m_Body, FlowCollector<T>([fn](T v) -> Task<> {
                (*fn)(v);
                return detail::Ready();
            }));
        }

        // What a remembered collection keys on: the same flow object, the same identity.
        const void* Identity() const { return m_Body.get(); }

    private:
        static Task<> Run(std::shared_ptr<const Body> body, FlowCollector<T> collector) { co_await (*body)(std::move(collector)); }
        std::shared_ptr<const Body> m_Body;
    };

    // flowOf / asFlow / emptyFlow
    template<typename T>
    Flow<T> FlowOf(std::vector<T> values) {
        return Flow<T>::Create([values = std::move(values)](FlowCollector<T> emit) -> Task<> {
            for (const T& v : values) co_await emit(v);
        });
    }
    template<typename T, typename... More>
    Flow<T> FlowOf(T first, More... more) { return FlowOf<T>(std::vector<T>{ std::move(first), T(std::move(more))... }); }
    template<typename T>
    Flow<T> EmptyFlow() { return Flow<T>(); }

    // --- Intermediate operators ------------------------------------------------------------------

    // transform { value -> emit(...) }: `fn(value, emit)` emits any number of values.
    template<typename T, typename R>
    Flow<R> Transform(Flow<T> upstream, kiln::Function<Task<>(T, FlowCollector<R>)> fn) {
        auto f = std::make_shared<kiln::Function<Task<>(T, FlowCollector<R>)>>(std::move(fn));
        return Flow<R>::Create([upstream = std::move(upstream), f](FlowCollector<R> emit) -> Task<> {
            co_await upstream.Collect(FlowCollector<T>([f, emit](T v) -> Task<> { return (*f)(std::move(v), emit); }));
        });
    }

    template<typename T, typename F>
    auto Map(Flow<T> upstream, F fn) -> Flow<std::invoke_result_t<F, const T&>> {
        using R = std::invoke_result_t<F, const T&>;
        auto f = std::make_shared<F>(std::move(fn));
        return Flow<R>::Create([upstream = std::move(upstream), f](FlowCollector<R> emit) -> Task<> {
            co_await upstream.Collect(FlowCollector<T>([f, emit](T v) -> Task<> { return emit((*f)(v)); }));
        });
    }
    // mapNotNull: values the function maps to something.
    template<typename T, typename F>
    auto MapNotNull(Flow<T> upstream, F fn) -> Flow<typename std::invoke_result_t<F, const T&>::value_type> {
        using R = typename std::invoke_result_t<F, const T&>::value_type;
        auto f = std::make_shared<F>(std::move(fn));
        return Flow<R>::Create([upstream = std::move(upstream), f](FlowCollector<R> emit) -> Task<> {
            co_await upstream.Collect(FlowCollector<T>([f, emit](T v) -> Task<> {
                auto r = (*f)(v);
                return r ? emit(std::move(*r)) : detail::Ready();
            }));
        });
    }
    template<typename T, typename F>
    Flow<T> Filter(Flow<T> upstream, F predicate) {
        auto p = std::make_shared<F>(std::move(predicate));
        return Flow<T>::Create([upstream = std::move(upstream), p](FlowCollector<T> emit) -> Task<> {
            co_await upstream.Collect(FlowCollector<T>([p, emit](T v) -> Task<> { return (*p)(v) ? emit(std::move(v)) : detail::Ready(); }));
        });
    }
    // onEach { }: a side effect before each value goes on.
    template<typename T, typename F>
    Flow<T> OnEach(Flow<T> upstream, F action) {
        auto a = std::make_shared<F>(std::move(action));
        return Flow<T>::Create([upstream = std::move(upstream), a](FlowCollector<T> emit) -> Task<> {
            co_await upstream.Collect(FlowCollector<T>([a, emit](T v) -> Task<> {
                (*a)(v);
                return emit(std::move(v));
            }));
        });
    }
    // onStart { emit -> }: before the upstream starts.
    template<typename T>
    Flow<T> OnStart(Flow<T> upstream, kiln::Function<Task<>(FlowCollector<T>)> action) {
        auto a = std::make_shared<kiln::Function<Task<>(FlowCollector<T>)>>(std::move(action));
        return Flow<T>::Create([upstream = std::move(upstream), a](FlowCollector<T> emit) -> Task<> {
            co_await (*a)(emit);
            co_await upstream.Collect(emit);
        });
    }
    // onCompletion { error -> }: after the upstream completes or fails (rethrown after).
    template<typename T>
    Flow<T> OnCompletion(Flow<T> upstream, kiln::Function<void(std::exception_ptr)> action) {
        auto a = std::make_shared<kiln::Function<void(std::exception_ptr)>>(std::move(action));
        return Flow<T>::Create([upstream = std::move(upstream), a](FlowCollector<T> emit) -> Task<> {
            std::exception_ptr error;
            try {
                co_await upstream.Collect(emit);
            } catch (const detail::AbortFlow&) {
                (*a)(nullptr);
                throw;
            } catch (...) {
                error = std::current_exception();
            }
            (*a)(error);
            if (error) std::rethrow_exception(error);
        });
    }
    // catch { error -> emit(...) }: an upstream failure handled (downstream failures pass).
    template<typename T>
    Flow<T> Catch(Flow<T> upstream, kiln::Function<Task<>(std::exception_ptr, FlowCollector<T>)> handler) {
        auto h = std::make_shared<kiln::Function<Task<>(std::exception_ptr, FlowCollector<T>)>>(std::move(handler));
        return Flow<T>::Create([upstream = std::move(upstream), h](FlowCollector<T> emit) -> Task<> {
            auto downstreamFailed = std::make_shared<bool>(false);
            std::exception_ptr error;
            try {
                co_await upstream.Collect(FlowCollector<T>([emit, downstreamFailed](T v) -> Task<> {
                    return [](FlowCollector<T> e, T value, std::shared_ptr<bool> failed) -> Task<> {
                        try {
                            co_await e(std::move(value));
                        } catch (...) {
                            *failed = true;
                            throw;
                        }
                    }(emit, std::move(v), downstreamFailed);
                }));
            } catch (const detail::AbortFlow&) {
                throw;
            } catch (...) {
                if (*downstreamFailed) throw;
                error = std::current_exception();
            }
            if (error) co_await (*h)(error, emit);
        });
    }
    // distinctUntilChanged: a value equal to the previous one is dropped.
    template<typename T>
    Flow<T> DistinctUntilChanged(Flow<T> upstream) {
        return Flow<T>::Create([upstream = std::move(upstream)](FlowCollector<T> emit) -> Task<> {
            auto last = std::make_shared<std::optional<T>>();
            co_await upstream.Collect(FlowCollector<T>([last, emit](T v) -> Task<> {
                if (*last && **last == v) return detail::Ready();
                *last = v;
                return emit(std::move(v));
            }));
        });
    }
    // take(n): the first n values, then upstream stops.
    template<typename T>
    Flow<T> Take(Flow<T> upstream, int count) {
        return Flow<T>::Create([upstream = std::move(upstream), count](FlowCollector<T> emit) -> Task<> {
            if (count <= 0) co_return;
            auto owner = std::make_shared<int>(0);
            try {
                co_await upstream.Collect(FlowCollector<T>([owner, emit, count](T v) -> Task<> {
                    return [](FlowCollector<T> e, T value, std::shared_ptr<int> seen, int n) -> Task<> {
                        co_await e(std::move(value));
                        if (++*seen >= n) throw detail::AbortFlow{ seen.get() };
                    }(emit, std::move(v), owner, count);
                }));
            } catch (const detail::AbortFlow& a) {
                if (a.owner != owner.get()) throw;
            }
        });
    }
    // drop(n): all but the first n values.
    template<typename T>
    Flow<T> Drop(Flow<T> upstream, int count) {
        return Flow<T>::Create([upstream = std::move(upstream), count](FlowCollector<T> emit) -> Task<> {
            auto seen = std::make_shared<int>(0);
            co_await upstream.Collect(FlowCollector<T>([seen, emit, count](T v) -> Task<> {
                return (*seen)++ < count ? detail::Ready() : emit(std::move(v));
            }));
        });
    }
    // scan(initial) { acc, value -> }: the running accumulation, starting with `initial`.
    template<typename T, typename R, typename F>
    Flow<R> Scan(Flow<T> upstream, R initial, F fn) {
        auto f = std::make_shared<F>(std::move(fn));
        return Flow<R>::Create([upstream = std::move(upstream), initial, f](FlowCollector<R> emit) -> Task<> {
            auto acc = std::make_shared<R>(initial);
            co_await emit(*acc);
            co_await upstream.Collect(FlowCollector<T>([acc, f, emit](T v) -> Task<> {
                *acc = (*f)(*acc, v);
                return emit(*acc);
            }));
        });
    }

    namespace detail {
        // Child collections hand values to the collecting frame over a channel: (index, value),
        // nullopt value when that child completed, with its failure if it failed.
        template<typename V>
        struct ChildEvent {
            int index = 0;
            std::optional<V> value;
            std::exception_ptr error;
        };
        template<typename V, typename T>
        Task<> ForwardChild(Flow<T> flow, int index, std::shared_ptr<Channel<ChildEvent<V>>> ch, kiln::Function<V(T)> wrap) {
            std::exception_ptr error;
            try {
                co_await flow.Collect(FlowCollector<T>([ch, index, wrap](T v) -> Task<> {
                    ch->TrySend(ChildEvent<V>{ index, wrap(std::move(v)), nullptr });
                    return Ready();
                }));
            } catch (...) {
                error = std::current_exception();
            }
            ch->TrySend(ChildEvent<V>{ index, std::nullopt, error });
        }
        template<typename V>
        std::shared_ptr<Channel<ChildEvent<V>>> EventChannel() {
            return std::make_shared<Channel<ChildEvent<V>>>(std::size_t(1) << 20);
        }
    }

    // combine(a, b) { a, b -> }: the latest of each, once both have emitted, on every change.
    template<typename A, typename B, typename F>
    auto Combine(Flow<A> a, Flow<B> b, F fn) -> Flow<std::invoke_result_t<F, const A&, const B&>> {
        using R = std::invoke_result_t<F, const A&, const B&>;
        using V = std::pair<std::optional<A>, std::optional<B>>;
        auto f = std::make_shared<F>(std::move(fn));
        return Flow<R>::Create([a = std::move(a), b = std::move(b), f](FlowCollector<R> emit) -> Task<> {
            auto ch = detail::EventChannel<V>();
            Job ja = Launch(Dispatcher::Main(), detail::ForwardChild<V, A>(a, 0, ch, [](A v) { return V{ std::move(v), std::nullopt }; }));
            Job jb = Launch(Dispatcher::Main(), detail::ForwardChild<V, B>(b, 1, ch, [](B v) { return V{ std::nullopt, std::move(v) }; }));
            std::optional<A> la;
            std::optional<B> lb;
            int done = 0;
            while (done < 2) {
                auto e = co_await ch->Next();
                if (!e) break;
                if (e->error) std::rethrow_exception(e->error);
                if (!e->value) { ++done; continue; }
                if (e->value->first) la = std::move(e->value->first);
                if (e->value->second) lb = std::move(e->value->second);
                if (la && lb) co_await emit((*f)(*la, *lb));
            }
        });
    }
    // zip(a, b) { a, b -> }: pairs in order; ends with the shorter.
    template<typename A, typename B, typename F>
    auto Zip(Flow<A> a, Flow<B> b, F fn) -> Flow<std::invoke_result_t<F, const A&, const B&>> {
        using R = std::invoke_result_t<F, const A&, const B&>;
        using V = std::pair<std::optional<A>, std::optional<B>>;
        auto f = std::make_shared<F>(std::move(fn));
        return Flow<R>::Create([a = std::move(a), b = std::move(b), f](FlowCollector<R> emit) -> Task<> {
            auto ch = detail::EventChannel<V>();
            Job ja = Launch(Dispatcher::Main(), detail::ForwardChild<V, A>(a, 0, ch, [](A v) { return V{ std::move(v), std::nullopt }; }));
            Job jb = Launch(Dispatcher::Main(), detail::ForwardChild<V, B>(b, 1, ch, [](B v) { return V{ std::nullopt, std::move(v) }; }));
            std::deque<A> qa;
            std::deque<B> qb;
            bool doneA = false, doneB = false;
            while (true) {
                while (!qa.empty() && !qb.empty()) {
                    A x = std::move(qa.front()); qa.pop_front();
                    B y = std::move(qb.front()); qb.pop_front();
                    co_await emit((*f)(x, y));
                }
                if ((doneA && qa.empty()) || (doneB && qb.empty())) break;
                auto e = co_await ch->Next();
                if (!e) break;
                if (e->error) std::rethrow_exception(e->error);
                if (!e->value) { (e->index == 0 ? doneA : doneB) = true; continue; }
                if (e->value->first) qa.push_back(std::move(*e->value->first));
                if (e->value->second) qb.push_back(std::move(*e->value->second));
            }
        });
    }
    // merge(flows): every value of every flow, as they come.
    template<typename T>
    Flow<T> Merge(std::vector<Flow<T>> flows) {
        return Flow<T>::Create([flows = std::move(flows)](FlowCollector<T> emit) -> Task<> {
            auto ch = detail::EventChannel<T>();
            std::vector<Job> jobs;
            for (std::size_t i = 0; i < flows.size(); ++i)
                jobs.push_back(Launch(Dispatcher::Main(), detail::ForwardChild<T, T>(flows[i], int(i), ch, [](T v) { return v; })));
            std::size_t done = 0;
            while (done < flows.size()) {
                auto e = co_await ch->Next();
                if (!e) break;
                if (e->error) std::rethrow_exception(e->error);
                if (!e->value) { ++done; continue; }
                co_await emit(std::move(*e->value));
            }
        });
    }
    // flatMapLatest { value -> flow }: the latest value's flow; a new value cancels the previous.
    template<typename T, typename F>
    auto FlatMapLatest(Flow<T> upstream, F fn) -> Flow<typename std::invoke_result_t<F, const T&>::value_type> {
        using R = typename std::invoke_result_t<F, const T&>::value_type;
        auto f = std::make_shared<F>(std::move(fn));
        return Flow<R>::Create([upstream = std::move(upstream), f](FlowCollector<R> emit) -> Task<> {
            // Events: index 0 = an upstream value (as the flow to switch to), 1 = an inner value.
            using V = std::variant<Flow<R>, R>;
            auto ch = detail::EventChannel<V>();
            Job outer = Launch(Dispatcher::Main(), detail::ForwardChild<V, T>(upstream, 0, ch, [f](T v) { return V((*f)(v)); }));
            Job inner;
            int generation = 0;
            bool outerDone = false, innerDone = true;
            while (!(outerDone && innerDone)) {
                auto e = co_await ch->Next();
                if (!e) break;
                if (e->error) std::rethrow_exception(e->error);
                if (!e->value) {
                    if (e->index == 0) outerDone = true;
                    else if (e->index == generation) innerDone = true;
                    continue;
                }
                if (auto* next = std::get_if<Flow<R>>(&*e->value)) {
                    inner.Cancel();
                    ++generation;
                    innerDone = false;
                    inner = Launch(Dispatcher::Main(), detail::ForwardChild<V, R>(*next, generation, ch, [](R v) { return V(std::move(v)); }));
                } else if (e->index == generation) {
                    co_await emit(std::move(std::get<R>(*e->value)));
                }
            }
        });
    }
    // debounce(ms): a value goes on once no newer one came within `ms`; the last on completion.
    template<typename T>
    Flow<T> Debounce(Flow<T> upstream, std::int64_t ms) {
        return Flow<T>::Create([upstream = std::move(upstream), ms](FlowCollector<T> emit) -> Task<> {
            // Index 0: upstream values; 1: the timer of the pending value's generation fired.
            using V = std::pair<std::optional<T>, int>;
            auto ch = detail::EventChannel<V>();
            Job source = Launch(Dispatcher::Main(), detail::ForwardChild<V, T>(upstream, 0, ch, [](T v) { return V{ std::move(v), 0 }; }));
            Job timer;
            std::optional<T> pending;
            int generation = 0;
            while (true) {
                auto e = co_await ch->Next();
                if (!e) break;
                if (e->error) std::rethrow_exception(e->error);
                if (e->index == 0 && !e->value) break;   // upstream completed
                if (e->index == 0) {
                    pending = std::move(e->value->first);
                    timer.Cancel();
                    const int gen = ++generation;
                    timer = Launch(Dispatcher::Main(), [](std::shared_ptr<Channel<detail::ChildEvent<V>>> c, std::int64_t wait, int g) -> Task<> {
                        co_await Delay(wait);
                        c->TrySend(detail::ChildEvent<V>{ 1, V{ std::nullopt, g }, nullptr });
                    }(ch, ms, gen));
                } else if (e->value && e->value->second == generation && pending) {
                    T v = std::move(*pending);
                    pending.reset();
                    co_await emit(std::move(v));
                }
            }
            timer.Cancel();
            if (pending) co_await emit(std::move(*pending));
        });
    }
    namespace detail {
        template<typename T>
        struct ConflateState {
            std::optional<T> latest;
            bool done = false;
            std::exception_ptr error;
            std::shared_ptr<Channel<bool>> wake = std::shared_ptr<Channel<bool>>(Channel<bool>::Conflated().release());
        };
        template<typename T>
        Task<> FeedLatest(Flow<T> upstream, std::shared_ptr<ConflateState<T>> st) {
            try {
                co_await upstream.Collect(kiln::Function<void(const T&)>([st](const T& v) {
                    st->latest = v;
                    st->wake->TrySend(true);
                }));
            } catch (...) {
                st->error = std::current_exception();
            }
            st->done = true;
            st->wake->TrySend(true);
        }
    }
    // conflate(): a slow collector gets the latest value, skipping those it missed; the last
    // value always arrives before completion.
    template<typename T>
    Flow<T> Conflate(Flow<T> upstream) {
        return Flow<T>::Create([upstream = std::move(upstream)](FlowCollector<T> emit) -> Task<> {
            auto st = std::make_shared<detail::ConflateState<T>>();
            Job source = Launch(Dispatcher::Main(), detail::FeedLatest<T>(upstream, st));
            while (true) {
                if (st->latest) {
                    T v = std::move(*st->latest);
                    st->latest.reset();
                    co_await emit(std::move(v));
                    continue;
                }
                if (st->done) break;
                if (!co_await st->wake->Next()) break;
            }
            if (st->error) std::rethrow_exception(st->error);
        });
    }

    // --- Terminal operators ----------------------------------------------------------------------

    // toList(): every value, once the flow completes.
    template<typename T>
    Task<std::vector<T>> ToList(Flow<T> flow) {
        std::vector<T> out;
        co_await flow.Collect(kiln::Function<void(const T&)>([&out](const T& v) { out.push_back(v); }));
        co_return out;
    }
    // first(): the first value (std::out_of_range when the flow is empty, as NoSuchElementException).
    template<typename T>
    Task<T> First(Flow<T> flow) {
        std::optional<T> out;
        auto owner = std::make_shared<int>(0);
        try {
            co_await flow.Collect(FlowCollector<T>([&out, owner](T v) -> Task<> {
                out = std::move(v);
                throw detail::AbortFlow{ owner.get() };
            }));
        } catch (const detail::AbortFlow& a) {
            if (a.owner != owner.get()) throw;
        }
        if (!out) throw std::out_of_range("Expected at least one element");
        co_return std::move(*out);
    }
    // firstOrNull { predicate }
    template<typename T, typename P>
    Task<std::optional<T>> FirstOrNull(Flow<T> flow, P predicate) {
        std::optional<T> out;
        auto owner = std::make_shared<int>(0);
        try {
            co_await flow.Collect(FlowCollector<T>([&out, owner, &predicate](T v) -> Task<> {
                if (!predicate(v)) return detail::Ready();
                out = std::move(v);
                throw detail::AbortFlow{ owner.get() };
            }));
        } catch (const detail::AbortFlow& a) {
            if (a.owner != owner.get()) throw;
        }
        co_return out;
    }
    // collectLatest { }: a new value cancels the handling of the previous one.
    template<typename T>
    Task<> CollectLatest(Flow<T> flow, kiln::Function<Task<>(T)> action) {
        auto a = std::make_shared<kiln::Function<Task<>(T)>>(std::move(action));
        auto current = std::make_shared<Job>();
        co_await flow.Collect(kiln::Function<void(const T&)>([a, current](const T& v) {
            current->Cancel();
            *current = Launch(Dispatcher::Main(), (*a)(v));
        }));
        // Wait for the last handler.
        while (!current->Done()) co_await Yield();
    }

    // --- Hot flows ---------------------------------------------------------------------------------

    // MutableStateFlow: a value with observers. Collecting emits the current value, then each
    // new one that differs (conflated: a slow collector sees the latest). Set on the main thread.
    template<typename T>
    class MutableStateFlow {
        struct Shared {
            T value;
            std::vector<std::weak_ptr<Channel<bool>>> subscribers;
            int subscriptionCount = 0;
            explicit Shared(T v) : value(std::move(v)) {}
        };

    public:
        explicit MutableStateFlow(T initial) : m_S(std::make_shared<Shared>(std::move(initial))) {}
        const T& Value() const { return m_S->value; }
        void SetValue(T v) {
            if (m_S->value == v) return;
            m_S->value = std::move(v);
            Notify();
        }
        // compareAndSet(expect, update)
        bool CompareAndSet(const T& expect, T update) {
            if (!(m_S->value == expect)) return false;
            SetValue(std::move(update));
            return true;
        }
        // update { }
        template<typename F> void Update(F fn) { SetValue(fn(m_S->value)); }
        // The number of active collectors (subscriptionCount, a plain count).
        int SubscriptionCount() const { return m_S->subscriptionCount; }
        const void* Identity() const { return m_S.get(); }
        Flow<T> AsFlow() const {
            return Flow<T>::Create([s = m_S](FlowCollector<T> emit) -> Task<> {
                auto wake = std::shared_ptr<Channel<bool>>(Channel<bool>::Conflated().release());
                s->subscribers.push_back(wake);
                ++s->subscriptionCount;
                struct Leave { std::shared_ptr<Shared> s; ~Leave() { --s->subscriptionCount; } } leave{ s };
                std::optional<T> last;
                while (true) {
                    if (!last || !(*last == s->value)) {
                        last = s->value;
                        co_await emit(*last);
                    }
                    if (!co_await wake->Next()) break;
                }
            });
        }
        Task<> Collect(kiln::Function<void(const T&)> collector) const { return AsFlow().Collect(std::move(collector)); }
        Task<> Collect(FlowCollector<T> collector) const { return AsFlow().Collect(std::move(collector)); }

    private:
        void Notify() {
            auto subs = m_S->subscribers;
            std::erase_if(m_S->subscribers, [](const auto& w) { return w.expired(); });
            for (auto& w : subs)
                if (auto c = w.lock()) c->TrySend(true);
        }
        std::shared_ptr<Shared> m_S;
    };
    template<typename T> using StateFlow = MutableStateFlow<T>;

    // MutableSharedFlow(replay, extraBufferCapacity): values to every collector, the last
    // `replay` of them to new collectors. TryEmit drops the oldest a slow collector has not
    // taken when its buffer is full (BufferOverflow.DROP_OLDEST).
    template<typename T>
    class MutableSharedFlow {
        struct Shared {
            int replay = 0;
            std::size_t buffer = 0;
            std::deque<T> cache;
            std::vector<std::weak_ptr<Channel<T>>> subscribers;
            int subscriptionCount = 0;
        };

    public:
        explicit MutableSharedFlow(int replay = 0, int extraBufferCapacity = 0) : m_S(std::make_shared<Shared>()) {
            m_S->replay = replay;
            m_S->buffer = std::size_t(std::max(1, replay + extraBufferCapacity + 64));
        }
        bool TryEmit(T v) {
            if (m_S->replay > 0) {
                m_S->cache.push_back(v);
                while (int(m_S->cache.size()) > m_S->replay) m_S->cache.pop_front();
            }
            auto subs = m_S->subscribers;
            std::erase_if(m_S->subscribers, [](const auto& w) { return w.expired(); });
            for (auto& w : subs)
                if (auto c = w.lock()) c->TrySend(v);
            return true;
        }
        Task<> Emit(T v) {
            TryEmit(std::move(v));
            co_return;
        }
        const std::deque<T>& ReplayCache() const { return m_S->cache; }
        void ResetReplayCache() { m_S->cache.clear(); }
        int SubscriptionCount() const { return m_S->subscriptionCount; }
        const void* Identity() const { return m_S.get(); }
        Flow<T> AsFlow() const {
            return Flow<T>::Create([s = m_S](FlowCollector<T> emit) -> Task<> {
                auto ch = std::make_shared<Channel<T>>(s->buffer, Channel<T>::Overflow::DropOldest);
                for (const T& v : s->cache) ch->TrySend(v);
                s->subscribers.push_back(ch);
                ++s->subscriptionCount;
                struct Leave { std::shared_ptr<Shared> s; ~Leave() { --s->subscriptionCount; } } leave{ s };
                while (true) {
                    auto v = co_await ch->Next();
                    if (!v) break;
                    co_await emit(std::move(*v));
                }
            });
        }
        Task<> Collect(kiln::Function<void(const T&)> collector) const { return AsFlow().Collect(std::move(collector)); }
        Task<> Collect(FlowCollector<T> collector) const { return AsFlow().Collect(std::move(collector)); }

    private:
        std::shared_ptr<Shared> m_S;
    };

    // SharingStarted: Eagerly (now), Lazily (with the first collector, never stops),
    // WhileSubscribed(stopTimeoutMs) (while there are collectors, stopping that long after the last).
    struct SharingStarted {
        enum class Kind : std::uint8_t { Eagerly, Lazily, WhileSubscribed } kind = Kind::Eagerly;
        std::int64_t stopTimeoutMs = 0;
        static SharingStarted Eagerly() { return { Kind::Eagerly, 0 }; }
        static SharingStarted Lazily() { return { Kind::Lazily, 0 }; }
        static SharingStarted WhileSubscribed(std::int64_t stopTimeoutMs = 0) { return { Kind::WhileSubscribed, stopTimeoutMs }; }
    };

    namespace detail {
        // The upstream collection a StateIn / ShareIn shares, started and stopped by its policy.
        struct Sharing {
            std::weak_ptr<CoroutineScope> scope;
            bool cancelled = false;   // the scope was cancelled: no restart
            kiln::Function<Task<>()> run;   // collects the upstream into the hot flow
            SharingStarted started;
            int collectors = 0;
            Job job, stopTimer;
            bool everStarted = false;
            void Start() {
                if (job.Active() || cancelled) return;
                everStarted = true;
                auto sc = scope.lock();
                job = Launch(Dispatcher::Main(), run(), sc ? sc->gate : nullptr);
            }
            void Subscribed() {
                ++collectors;
                stopTimer.Cancel();
                if (started.kind != SharingStarted::Kind::Eagerly) Start();
            }
            void Unsubscribed(const std::shared_ptr<Sharing>& self) {
                if (--collectors > 0 || started.kind != SharingStarted::Kind::WhileSubscribed) return;
                std::weak_ptr<Sharing> weak = self;
                stopTimer = Launch(Dispatcher::Main(), [](std::weak_ptr<Sharing> w, std::int64_t ms) -> Task<> {
                    co_await Delay(ms);
                    if (auto s = w.lock()) if (s->collectors == 0) s->job.Cancel();
                }(weak, started.stopTimeoutMs));
            }
        };
        // A job in the scope whose end (the scope cancelled, a composition left) stops the sharing.
        inline Task<> GuardSharing(std::weak_ptr<Sharing> sharing) {
            struct Stop {
                std::weak_ptr<Sharing> s;
                ~Stop() {
                    if (auto p = s.lock()) {
                        p->cancelled = true;
                        p->job.Cancel();
                        p->stopTimer.Cancel();
                    }
                }
            } stop{ sharing };
            auto never = std::make_shared<Channel<bool>>(1);
            co_await never->Next();
        }
        inline void AttachToScope(const std::shared_ptr<CoroutineScope>& scope, const std::shared_ptr<Sharing>& sharing) {
            if (scope) scope->Launch(GuardSharing(sharing));
        }
        template<typename T>
        Task<> CollectInto(Flow<T> upstream, MutableStateFlow<T> state) {
            co_await upstream.Collect(kiln::Function<void(const T&)>([state](const T& v) mutable { state.SetValue(v); }));
        }
        template<typename T>
        Task<> EmitInto(Flow<T> upstream, MutableSharedFlow<T> shared) {
            co_await upstream.Collect(kiln::Function<void(const T&)>([shared](const T& v) mutable { shared.TryEmit(v); }));
        }
    }

    // A hot flow over a shared upstream: what StateIn / ShareIn return.
    template<typename T, typename Hot>
    class SharedUpstream {
    public:
        SharedUpstream(Hot hot, std::shared_ptr<detail::Sharing> sharing) : m_Hot(std::move(hot)), m_Sharing(std::move(sharing)) {}
        const Hot& Hot_() const { return m_Hot; }
        const void* Identity() const { return m_Sharing.get(); }
        Flow<T> AsFlow() const {
            return Flow<T>::Create([hot = m_Hot, sharing = m_Sharing](FlowCollector<T> emit) -> Task<> {
                sharing->Subscribed();
                struct Leave {
                    std::shared_ptr<detail::Sharing> s;
                    ~Leave() { s->Unsubscribed(s); }
                } leave{ sharing };
                co_await hot.Collect(emit);
            });
        }
        Task<> Collect(kiln::Function<void(const T&)> collector) const { return AsFlow().Collect(std::move(collector)); }
        Task<> Collect(FlowCollector<T> collector) const { return AsFlow().Collect(std::move(collector)); }

    protected:
        Hot m_Hot;
        std::shared_ptr<detail::Sharing> m_Sharing;
    };
    template<typename T>
    class SharedStateFlow : public SharedUpstream<T, MutableStateFlow<T>> {
    public:
        using SharedUpstream<T, MutableStateFlow<T>>::SharedUpstream;
        const T& Value() const { return this->m_Hot.Value(); }
    };

    // stateIn(scope, started, initialValue): the upstream's latest value as a state flow.
    template<typename T>
    SharedStateFlow<T> StateIn(Flow<T> upstream, std::shared_ptr<CoroutineScope> scope, SharingStarted started, T initialValue) {
        MutableStateFlow<T> state(std::move(initialValue));
        auto sharing = std::make_shared<detail::Sharing>();
        sharing->scope = scope;
        sharing->started = started;
        sharing->run = [upstream = std::move(upstream), state] { return detail::CollectInto<T>(upstream, state); };
        detail::AttachToScope(scope, sharing);
        if (started.kind == SharingStarted::Kind::Eagerly) sharing->Start();
        return SharedStateFlow<T>(state, sharing);
    }
    // shareIn(scope, started, replay)
    template<typename T>
    SharedUpstream<T, MutableSharedFlow<T>> ShareIn(Flow<T> upstream, std::shared_ptr<CoroutineScope> scope, SharingStarted started, int replay = 0) {
        MutableSharedFlow<T> shared(replay);
        auto sharing = std::make_shared<detail::Sharing>();
        sharing->scope = scope;
        sharing->started = started;
        sharing->run = [upstream = std::move(upstream), shared] { return detail::EmitInto<T>(upstream, shared); };
        detail::AttachToScope(scope, sharing);
        if (started.kind == SharingStarted::Kind::Eagerly) sharing->Start();
        return SharedUpstream<T, MutableSharedFlow<T>>(shared, sharing);
    }

    // collectAsState(initial): the flow's latest value as state, collected while the caller is
    // composed (restarted when given another flow). StateFlows start with their value.
    template<typename T, typename F>
    State<T>& CollectAsState(const F& flow, T initial, Key key = {}) {
        auto& c = *Composition::Current();
        auto& state = c.Remember<std::shared_ptr<State<T>>>([&] { return std::make_shared<State<T>>(std::move(initial)); }, {}, key);
        std::shared_ptr<State<T>> out = state;
        c.LaunchedEffect(Keys(reinterpret_cast<std::uintptr_t>(flow.Identity())), [flow, out] {
            return flow.Collect(kiln::Function<void(const T&)>([out](const T& v) { out->Set(v); }));
        }, key);
        return *state;
    }
    template<typename T>
    State<T>& CollectAsState(const MutableStateFlow<T>& flow, Key key = {}) { return CollectAsState<T>(flow, flow.Value(), key); }
    template<typename T>
    State<T>& CollectAsState(const SharedStateFlow<T>& flow, Key key = {}) { return CollectAsState<T>(flow, flow.Value(), key); }

}
