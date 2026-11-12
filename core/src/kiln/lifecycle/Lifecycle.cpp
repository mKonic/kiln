#include "kiln/lifecycle/Lifecycle.h"

#include "kiln/runtime/Channel.h"

#include <algorithm>
#include <string>

namespace kiln::lifecycle {

    // ---- Event / State ---------------------------------------------------------------------

    State TargetState(Event e) {
        switch (e) {
            case Event::OnCreate:
            case Event::OnStop: return State::Created;
            case Event::OnStart:
            case Event::OnPause: return State::Started;
            case Event::OnResume: return State::Resumed;
            case Event::OnDestroy: return State::Destroyed;
            case Event::OnAny: break;
        }
        throw std::invalid_argument("ON_ANY has no target state");
    }
    std::optional<Event> DownFrom(State s) {
        switch (s) {
            case State::Created: return Event::OnDestroy;
            case State::Started: return Event::OnStop;
            case State::Resumed: return Event::OnPause;
            default: return std::nullopt;
        }
    }
    std::optional<Event> DownTo(State s) {
        switch (s) {
            case State::Destroyed: return Event::OnDestroy;
            case State::Created: return Event::OnStop;
            case State::Started: return Event::OnPause;
            default: return std::nullopt;
        }
    }
    std::optional<Event> UpFrom(State s) {
        switch (s) {
            case State::Initialized: return Event::OnCreate;
            case State::Created: return Event::OnStart;
            case State::Started: return Event::OnResume;
            default: return std::nullopt;
        }
    }
    std::optional<Event> UpTo(State s) {
        switch (s) {
            case State::Created: return Event::OnCreate;
            case State::Started: return Event::OnStart;
            case State::Resumed: return Event::OnResume;
            default: return std::nullopt;
        }
    }
    const char* ToString(State s) {
        static const char* names[] = { "DESTROYED", "INITIALIZED", "CREATED", "STARTED", "RESUMED" };
        return names[int(s)];
    }
    const char* ToString(Event e) {
        static const char* names[] = { "ON_CREATE", "ON_START", "ON_RESUME", "ON_PAUSE", "ON_STOP", "ON_DESTROY", "ON_ANY" };
        return names[int(e)];
    }

    // ---- observers -------------------------------------------------------------------------

    namespace {
        class CallbackObserver final : public LifecycleEventObserver {
        public:
            explicit CallbackObserver(std::function<void(LifecycleOwner&, Event)> f) : m_F(std::move(f)) {}
            void OnStateChanged(LifecycleOwner& source, Event event) override { m_F(source, event); }
        private:
            std::function<void(LifecycleOwner&, Event)> m_F;
        };

        // Lifecycling.lifecycleEventObserver / DefaultLifecycleObserverAdapter: a
        // DefaultLifecycleObserver's callback first, then its event callback if it has both.
        class Adapter final : public LifecycleEventObserver {
        public:
            Adapter(DefaultLifecycleObserver* d, LifecycleEventObserver* e) : m_Default(d), m_Event(e) {}
            void OnStateChanged(LifecycleOwner& source, Event event) override {
                switch (event) {
                    case Event::OnCreate: m_Default->OnCreate(source); break;
                    case Event::OnStart: m_Default->OnStart(source); break;
                    case Event::OnResume: m_Default->OnResume(source); break;
                    case Event::OnPause: m_Default->OnPause(source); break;
                    case Event::OnStop: m_Default->OnStop(source); break;
                    case Event::OnDestroy: m_Default->OnDestroy(source); break;
                    case Event::OnAny: throw std::invalid_argument("ON_ANY must not be sent by anybody");
                }
                if (m_Event) m_Event->OnStateChanged(source, event);
            }
        private:
            DefaultLifecycleObserver* m_Default;
            LifecycleEventObserver* m_Event;
        };

        class SimpleOwner final : public LifecycleOwner {
        public:
            explicit SimpleOwner(std::shared_ptr<Lifecycle> l) : m_L(std::move(l)) {}
            Lifecycle& GetLifecycle() override { return *m_L; }
        private:
            std::shared_ptr<Lifecycle> m_L;
        };
    }

    std::shared_ptr<LifecycleEventObserver> MakeEventObserver(std::function<void(LifecycleOwner&, Event)> action) {
        return std::make_shared<CallbackObserver>(std::move(action));
    }
    std::shared_ptr<LifecycleEventObserver> AddObserver(Lifecycle& lifecycle, std::function<void(LifecycleOwner&, Event)> action) {
        auto o = MakeEventObserver(std::move(action));
        lifecycle.AddObserver(o);
        return o;
    }
    std::shared_ptr<LifecycleOwner> MakeLifecycleOwner(std::shared_ptr<Lifecycle> lifecycle) {
        return std::make_shared<SimpleOwner>(std::move(lifecycle));
    }

    // ---- Lifecycle -------------------------------------------------------------------------

    Lifecycle::~Lifecycle() {
        if (m_Scope) m_Scope->Cancel();
    }

    const rt::State<State>& Lifecycle::CurrentStateFlow() {
        if (!m_StateFlow) {
            m_StateFlow = std::make_unique<rt::State<State>>(CurrentState());
            rt::State<State>* flow = m_StateFlow.get();
            m_StateFlowObserver = MakeEventObserver([flow](LifecycleOwner&, Event e) { flow->Set(TargetState(e)); });
            AddObserver(m_StateFlowObserver);
        }
        return *m_StateFlow;
    }

    std::shared_ptr<LifecycleCoroutineScope> Lifecycle::CoroutineScope() {
        if (!m_Scope) {
            m_Scope = std::make_shared<LifecycleCoroutineScope>(*this);
            if (CurrentState() == State::Destroyed) m_Scope->Cancel();
            m_Scope->Register();
        }
        return m_Scope;
    }

    // ---- LifecycleCoroutineScope -----------------------------------------------------------

    void LifecycleCoroutineScope::Launch(rt::Task<> task) {
        if (m_Cancelled) return;
        rt::CoroutineScope::Launch(std::move(task));
    }
    void LifecycleCoroutineScope::Register() {
        if (m_Cancelled) return;
        if (m_Lifecycle->CurrentState() >= State::Initialized) m_Lifecycle->AddObserver(shared_from_this());
        else Cancel();
    }
    void LifecycleCoroutineScope::OnStateChanged(LifecycleOwner&, Event) {
        if (m_Lifecycle->CurrentState() <= State::Destroyed) {
            m_Lifecycle->RemoveObserver(shared_from_this());
            Cancel();
        }
    }
    void LifecycleCoroutineScope::Cancel() {
        m_Cancelled = true;
        auto cancelled = std::move(jobs);   // ~Job cancels
        jobs.clear();
    }

    // ---- eventFlow -------------------------------------------------------------------------

    rt::Task<> LifecycleEventFlow::Collect(std::function<void(Event)> collector) const {
        Lifecycle* lifecycle = m_Lifecycle;
        auto channel = std::make_shared<rt::Channel<Event>>(64);
        std::weak_ptr<rt::Channel<Event>> weak = channel;
        auto observer = AddObserver(*lifecycle, [weak](LifecycleOwner&, Event e) {
            if (auto ch = weak.lock()) {
                ch->TrySend(e);
                if (e == Event::OnDestroy) ch->Close();
            }
        });
        // awaitClose { removeObserver }: also when the collector is cancelled.
        struct Remove {
            Lifecycle* l; std::shared_ptr<LifecycleEventObserver> o;
            ~Remove() { l->RemoveObserver(o); }
        } remove{ lifecycle, observer };
        while (auto e = co_await channel->Next()) collector(*e);
    }

    // ---- LifecycleRegistry -----------------------------------------------------------------

    struct LifecycleRegistry::ObserverWithState {
        State state;
        std::shared_ptr<LifecycleObserver> observer;
        std::shared_ptr<LifecycleEventObserver> adapted;   // observer itself, or an adapter
        LifecycleEventObserver* target = nullptr;

        ObserverWithState(std::shared_ptr<LifecycleObserver> o, State initial) : state(initial), observer(std::move(o)) {
            auto* d = dynamic_cast<DefaultLifecycleObserver*>(observer.get());
            auto* e = dynamic_cast<LifecycleEventObserver*>(observer.get());
            if (d) {
                adapted = std::make_shared<Adapter>(d, e);
                target = adapted.get();
            } else if (e) {
                target = e;
            } else {
                throw std::invalid_argument("a LifecycleObserver must be a LifecycleEventObserver or a DefaultLifecycleObserver");
            }
        }
        void DispatchEvent(LifecycleOwner& owner, Event event) {
            const State newState = TargetState(event);
            state = std::min(state, newState);
            target->OnStateChanged(owner, event);
            state = newState;
        }
    };

    LifecycleRegistry::LifecycleRegistry(LifecycleOwner& provider) : m_Owner(&provider) {}
    LifecycleRegistry::~LifecycleRegistry() { Clear(); }

    void LifecycleRegistry::Unlink(const std::shared_ptr<Entry>& e) {
        auto prev = e->prev;
        if (!prev) m_Head = e->next; else prev->next = e->next;
        if (!e->next) m_Tail = prev; else e->next->prev = prev;
        e->removed = true;   // keeps its links: a pass standing on it walks on from there
    }
    void LifecycleRegistry::Clear() {
        // Removed entries keep their links but nothing points at them; live ones point at each
        // other, so break those.
        for (auto& [k, e] : m_Map) {
            e->removed = true;
            e->next.reset();
            e->prev.reset();
        }
        m_Map.clear();
        m_Head.reset();
        m_Tail.reset();
    }

    void LifecycleRegistry::SetCurrentState(State state) { MoveToState(state); }
    void LifecycleRegistry::HandleLifecycleEvent(Event event) { MoveToState(TargetState(event)); }

    void LifecycleRegistry::MoveToState(State next) {
        if (m_State == next) return;
        if (m_State == State::Initialized && next == State::Destroyed)
            throw std::logic_error(std::string("State must be at least 'CREATED' to be moved to '") + ToString(next) + "'");
        if (m_State == State::Destroyed)
            throw std::logic_error(std::string("State is 'DESTROYED' and cannot be moved to '") + ToString(next) + "'");
        m_State = next;
        if (m_HandlingEvent || m_AddingObserverCounter != 0) {
            m_NewEventOccurred = true;
            return;
        }
        m_HandlingEvent = true;
        Sync();
        m_HandlingEvent = false;
        if (m_State == State::Destroyed) Clear();
    }

    bool LifecycleRegistry::IsSynced() const {
        if (m_Map.empty()) return true;
        const State eldest = m_Head->value->state;
        const State newest = m_Tail->value->state;
        return eldest == newest && m_State == newest;
    }

    State LifecycleRegistry::CalculateTargetState(const LifecycleObserver* observer) const {
        State sibling = State::Resumed;
        if (auto it = m_Map.find(observer); it != m_Map.end())
            if (auto& prev = it->second->prev) sibling = prev->value->state;
        const State parent = m_ParentStates.empty() ? State::Resumed : m_ParentStates.back();
        return std::min({ m_State, sibling, parent });
    }

    void LifecycleRegistry::AddObserver(std::shared_ptr<LifecycleObserver> observer) {
        const State initial = m_State == State::Destroyed ? State::Destroyed : State::Initialized;
        const LifecycleObserver* key = observer.get();
        if (m_Map.contains(key)) return;
        auto stateful = std::make_shared<ObserverWithState>(observer, initial);
        auto entry = std::make_shared<Entry>();
        entry->key = std::move(observer);
        entry->value = stateful;
        if (!m_Tail) {
            m_Head = m_Tail = entry;
        } else {
            m_Tail->next = entry;
            entry->prev = m_Tail;
            m_Tail = entry;
        }
        m_Map.emplace(key, entry);

        const bool reentrance = m_AddingObserverCounter != 0 || m_HandlingEvent;
        State target = CalculateTargetState(key);
        ++m_AddingObserverCounter;
        while (stateful->state < target && m_Map.contains(key)) {
            m_ParentStates.push_back(stateful->state);
            auto event = UpFrom(stateful->state);
            if (!event) throw std::logic_error(std::string("no event up from ") + ToString(stateful->state));
            stateful->DispatchEvent(*m_Owner, *event);
            m_ParentStates.pop_back();
            target = CalculateTargetState(key);
        }
        if (!reentrance) Sync();
        --m_AddingObserverCounter;
    }

    void LifecycleRegistry::RemoveObserver(const std::shared_ptr<LifecycleObserver>& observer) {
        auto it = m_Map.find(observer.get());
        if (it == m_Map.end()) return;
        auto entry = it->second;
        m_Map.erase(it);
        Unlink(entry);
    }

    void LifecycleRegistry::ForwardPass() {
        for (std::shared_ptr<Entry> e = m_Head; e; e = e->next) {
            if (e->removed) continue;
            auto observer = e->value;
            const LifecycleObserver* key = e->key.get();
            while (observer->state < m_State && !m_NewEventOccurred && m_Map.contains(key)) {
                m_ParentStates.push_back(observer->state);
                auto event = UpFrom(observer->state);
                if (!event) throw std::logic_error(std::string("no event up from ") + ToString(observer->state));
                observer->DispatchEvent(*m_Owner, *event);
                m_ParentStates.pop_back();
            }
        }
    }

    void LifecycleRegistry::BackwardPass() {
        for (std::shared_ptr<Entry> e = m_Tail; e; e = e->prev) {
            if (e->removed) continue;
            auto observer = e->value;
            const LifecycleObserver* key = e->key.get();
            while (observer->state > m_State && !m_NewEventOccurred && m_Map.contains(key)) {
                auto event = DownFrom(observer->state);
                if (!event) throw std::logic_error(std::string("no event down from ") + ToString(observer->state));
                m_ParentStates.push_back(TargetState(*event));
                observer->DispatchEvent(*m_Owner, *event);
                m_ParentStates.pop_back();
            }
        }
    }

    void LifecycleRegistry::Sync() {
        while (!IsSynced()) {
            m_NewEventOccurred = false;
            if (m_State < m_Head->value->state) BackwardPass();
            if (!m_NewEventOccurred && m_Tail && m_State > m_Tail->value->state) ForwardPass();
        }
        m_NewEventOccurred = false;
        m_StateFlowValue.Set(m_State);
    }

}
