#pragma once

// androidx.lifecycle (lifecycle-common, lifecycle-runtime): a component's lifecycle as states
// and the events between them, observers told of each event in order, and LifecycleRegistry,
// the implementation hosts and navigation entries drive.
//   - Observers are kept by identity (shared_ptr); one added late is brought up to the current
//     state event by event, and an observer added from inside a callback never gets ahead of
//     the observers added before it (LifecycleRegistry's parentStates).
//   - CurrentStateFlow is an observable rt::State: read in composition or a snapshotFlow, it
//     follows the lifecycle (Lifecycle.currentStateFlow).
// Main thread only, as upstream (kiln has one UI thread; the checks are not repeated here).

#include "kiln/runtime/CoroutineScope.h"
#include <kiln/Function.h>
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace kiln::lifecycle {

    class LifecycleOwner;
    class LifecycleCoroutineScope;

    // LifecycleObserver: a marker; implement LifecycleEventObserver and/or DefaultLifecycleObserver.
    class LifecycleObserver {
    public:
        virtual ~LifecycleObserver() = default;
    };

    class Lifecycle {
    public:
        enum class State : std::uint8_t { Destroyed, Initialized, Created, Started, Resumed };
        enum class Event : std::uint8_t { OnCreate, OnStart, OnResume, OnPause, OnStop, OnDestroy, OnAny };

        virtual ~Lifecycle();
        virtual void AddObserver(std::shared_ptr<LifecycleObserver> observer) = 0;
        virtual void RemoveObserver(const std::shared_ptr<LifecycleObserver>& observer) = 0;
        virtual State CurrentState() const = 0;
        // The state as observable state (currentStateFlow).
        virtual const rt::State<State>& CurrentStateFlow();

        // A scope whose tasks are cancelled when the lifecycle is destroyed (Lifecycle.coroutineScope).
        std::shared_ptr<LifecycleCoroutineScope> CoroutineScope();

    private:
        std::unique_ptr<rt::State<State>> m_StateFlow;
        std::shared_ptr<LifecycleObserver> m_StateFlowObserver;
        std::shared_ptr<LifecycleCoroutineScope> m_Scope;
    };

    using State = Lifecycle::State;
    using Event = Lifecycle::Event;

    inline bool IsAtLeast(State s, State than) { return s >= than; }
    // Event.targetState: throws for OnAny.
    State TargetState(Event e);
    // Event.downFrom / downTo / upFrom / upTo.
    std::optional<Event> DownFrom(State s);
    std::optional<Event> DownTo(State s);
    std::optional<Event> UpFrom(State s);
    std::optional<Event> UpTo(State s);
    const char* ToString(State s);
    const char* ToString(Event e);

    class LifecycleOwner {
    public:
        virtual ~LifecycleOwner() = default;
        virtual Lifecycle& GetLifecycle() = 0;
    };

    // LifecycleOwner(lifecycle) (LifecycleOwnerFactory): an owner exposing `lifecycle`.
    std::shared_ptr<LifecycleOwner> MakeLifecycleOwner(std::shared_ptr<Lifecycle> lifecycle);

    // LifecycleEventObserver: every event.
    class LifecycleEventObserver : public virtual LifecycleObserver {
    public:
        virtual void OnStateChanged(LifecycleOwner& source, Event event) = 0;
    };

    // DefaultLifecycleObserver: one callback per event, each a no-op by default.
    class DefaultLifecycleObserver : public virtual LifecycleObserver {
    public:
        virtual void OnCreate(LifecycleOwner&) {}
        virtual void OnStart(LifecycleOwner&) {}
        virtual void OnResume(LifecycleOwner&) {}
        virtual void OnPause(LifecycleOwner&) {}
        virtual void OnStop(LifecycleOwner&) {}
        virtual void OnDestroy(LifecycleOwner&) {}
    };

    // Lifecycle.addObserver { source, event -> }: adds and returns a callback observer.
    std::shared_ptr<LifecycleEventObserver> AddObserver(Lifecycle& lifecycle, kiln::Function<void(LifecycleOwner&, Event)> action);
    // LifecycleEventObserver { source, event -> } without adding it.
    std::shared_ptr<LifecycleEventObserver> MakeEventObserver(kiln::Function<void(LifecycleOwner&, Event)> action);

    // LifecycleRegistry: a Lifecycle moved by HandleLifecycleEvent / SetCurrentState. It keeps a
    // pointer to its owner, which must outlive it or be the object holding it.
    class LifecycleRegistry : public Lifecycle {
    public:
        explicit LifecycleRegistry(LifecycleOwner& provider);
        ~LifecycleRegistry() override;
        // createUnsafe: the same here (no thread checks either way).
        static std::unique_ptr<LifecycleRegistry> CreateUnsafe(LifecycleOwner& owner) { return std::make_unique<LifecycleRegistry>(owner); }

        void AddObserver(std::shared_ptr<LifecycleObserver> observer) override;
        void RemoveObserver(const std::shared_ptr<LifecycleObserver>& observer) override;
        State CurrentState() const override { return m_State; }
        // Moves to `state`, sending the events between. Initialized to Destroyed and leaving
        // Destroyed throw std::logic_error.
        virtual void SetCurrentState(State state);
        virtual void HandleLifecycleEvent(Event event);
        virtual int ObserverCount() const { return int(m_Map.size()); }
        const rt::State<State>& CurrentStateFlow() override { return m_StateFlowValue; }

    private:
        struct ObserverWithState;
        struct Entry {
            std::shared_ptr<LifecycleObserver> key;
            std::shared_ptr<ObserverWithState> value;
            std::shared_ptr<Entry> next;
            std::shared_ptr<Entry> prev;   // strong both ways: Clear breaks the live list's links
            bool removed = false;
        };
        // FastSafeIterableMap: insertion order, removal keeps the links of a removed entry so a
        // pass standing on it continues.
        std::unordered_map<const LifecycleObserver*, std::shared_ptr<Entry>> m_Map;
        std::shared_ptr<Entry> m_Head, m_Tail;
        void Unlink(const std::shared_ptr<Entry>& e);
        void Clear();

        void MoveToState(State next);
        bool IsSynced() const;
        State CalculateTargetState(const LifecycleObserver* observer) const;
        void ForwardPass();
        void BackwardPass();
        void Sync();

        LifecycleOwner* m_Owner;
        State m_State = State::Initialized;
        rt::State<State> m_StateFlowValue{ State::Initialized };
        int m_AddingObserverCounter = 0;
        bool m_HandlingEvent = false;
        bool m_NewEventOccurred = false;
        std::vector<State> m_ParentStates;
    };

    // LifecycleCoroutineScope (lifecycleScope): tasks launched here are cancelled when the
    // lifecycle reaches Destroyed; on a destroyed lifecycle Launch does nothing.
    class LifecycleCoroutineScope : public rt::CoroutineScope, public LifecycleEventObserver,
                                    public std::enable_shared_from_this<LifecycleCoroutineScope> {
    public:
        explicit LifecycleCoroutineScope(Lifecycle& lifecycle) : m_Lifecycle(&lifecycle) {}
        Lifecycle& GetLifecycle() const { return *m_Lifecycle; }
        void Launch(rt::Task<> task);
        bool IsActive() const { return !m_Cancelled; }
        void OnStateChanged(LifecycleOwner& source, Event event) override;
        void Register();
        void Cancel();

    private:
        Lifecycle* m_Lifecycle;
        bool m_Cancelled = false;
    };

    // LifecycleOwner.lifecycleScope.
    inline std::shared_ptr<LifecycleCoroutineScope> LifecycleScope(LifecycleOwner& owner) { return owner.GetLifecycle().CoroutineScope(); }

    // Lifecycle.eventFlow: each event as it happens; the stream ends after OnDestroy.
    class LifecycleEventFlow {
    public:
        explicit LifecycleEventFlow(Lifecycle& lifecycle) : m_Lifecycle(&lifecycle) {}
        rt::Task<> Collect(kiln::Function<void(Event)> collector) const;
    private:
        Lifecycle* m_Lifecycle;
    };
    inline LifecycleEventFlow EventFlow(Lifecycle& lifecycle) { return LifecycleEventFlow(lifecycle); }

}
