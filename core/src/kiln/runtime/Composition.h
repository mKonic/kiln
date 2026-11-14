#pragma once

// Recompose scopes without a compiler plugin (architecture 3.7, spec 14 req. 2-5).
//
// A scope is a build function. Running it records the state it reads (it is a Subscriber) and
// the calls it makes, in order: remembered slots, emitted nodes, child scopes and effects. Each
// call is a "group" matched against the previous run's groups by kind + type + position, or by
// an explicit key. Unmatched old groups are disposed after the run. A state change re-runs only
// the scopes that read it; child scopes re-run with their parent unless their memo keys are
// unchanged (the stand-in for Compose's skipping of stable, equal parameters).
//
// Frame order (architecture 3.3): Recompose() applies state changes, re-runs invalid scopes
// top-down, updates node child lists, destroys removed nodes, then runs effects.

#include "kiln/runtime/State.h"
#include <kiln/Function.h>
#include "kiln/runtime/Task.h"

#include <any>
#include <type_traits>
#include <cstdint>
#include <functional>
#include <memory>
#include <source_location>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kiln::rt {

    // A node of the tree the composition builds (M2's LayoutNode derives from this).
    class TreeNode {
    public:
        virtual ~TreeNode() = default;
        // The full ordered child list after a recomposition touched this node's children.
        virtual void SetChildren(std::vector<TreeNode*> children) = 0;
        // compositeKeyHash: the composite key of the call that emitted it (stable across runs
        // and process restarts).
        std::uint64_t CompositeKeyHash() const { return m_CompositeKeyHash; }
        std::uint64_t m_CompositeKeyHash = 0;
        // The scope of the node's content: where it was composed, for reading the
        // CompositionLocals there (CompositionLocalConsumerModifierNode). Null outside a composition.
        class Scope* ContentScope() const { return m_ContentScope; }
        class Scope* m_ContentScope = nullptr;
        // Reuse (ComposeNodeLifecycleCallback): the composition that emitted the node was
        // deactivated and keeps it for other content (OnDeactivate), then that content took it
        // over (OnReuse, before its update runs).
        virtual void OnDeactivate() {}
        virtual void OnReuse() {}
        bool m_Deactivated = false;
    };

    // Identity of a call. Default-constructed at a call site it is that site's source location,
    // so an `if` that removes one call does not shift the identity of the calls after it (what
    // Compose gets from its compiler plugin). Repeated calls from one site (a loop) match by
    // order; give them explicit keys when items can move. Value 0 is purely positional (C ABI).
    struct Key {
        std::uint64_t value = 0;
        bool explicitKey = false;
        Key(std::source_location loc = std::source_location::current())
            : value((std::hash<std::string_view>{}(loc.file_name()) ^ (std::uint64_t(loc.line()) << 20)
                     ^ std::uint64_t(loc.column())) | 1) {}
        static Key Positional() { Key k; k.value = 0; return k; }
        Key(std::int64_t v) : value(std::uint64_t(v) * 0x9E3779B97F4A7C15ull + 1), explicitKey(true) {}
        Key(int v) : Key(std::int64_t(v)) {}
        Key(std::string_view s) : value(std::hash<std::string_view>{}(s) | 1), explicitKey(true) {}
        Key(const char* s) : Key(std::string_view(s)) {}
        bool operator==(const Key& o) const { return value == o.value; }
    };

    // Values whose change re-runs a remember initialiser, restarts an effect, or stops a child
    // scope from being skipped. Compared by hash of each value.
    struct Keys {
        std::vector<std::size_t> hashes;
        Keys() = default;
        template<typename... A> explicit Keys(const A&... a) : hashes{ std::hash<A>{}(a)... } {}
        bool operator==(const Keys&) const = default;
    };

    struct LocalAccessorScope;

    // A CompositionLocal (compositionLocalOf / staticCompositionLocalOf): a value provided to
    // the content below a Provide, read with Composition::Read; the default where none is.
    template<typename T>
    class Local {
    public:
        explicit Local(T defaultValue) : m_Default(std::move(defaultValue)) {}
        // compositionLocalWithComputedDefaultOf: the default computed where it is read, from other
        // locals' values there (read through the accessor).
        struct ComputedTag {};
        Local(ComputedTag, kiln::Function<T(const LocalAccessorScope&)> computeDefault) : m_Compute(std::move(computeDefault)) {}
        const T& Default() const { return m_Default; }
        bool HasComputedDefault() const { return bool(m_Compute); }
        T ComputeDefault(const LocalAccessorScope& scope) const { return m_Compute(scope); }
    private:
        T m_Default{};
        kiln::Function<T(const LocalAccessorScope&)> m_Compute;
    };

    template<typename T>
    Local<T> LocalWithComputedDefault(kiln::Function<T(const LocalAccessorScope&)> computeDefault) {
        return Local<T>(typename Local<T>::ComputedTag{}, std::move(computeDefault));
    }

    // RememberObserver: a remembered value told when it enters the composition (onRemembered,
    // after the changes apply) and when it leaves (onForgotten: its group was removed, its keys
    // changed or the composition was disposed). kiln applies every composition it runs, so
    // onAbandoned is never called.
    class RememberObserver {
    public:
        virtual ~RememberObserver() = default;
        virtual void OnRemembered() {}
        virtual void OnForgotten() {}
        virtual void OnAbandoned() {}
    };

    class Composition;
    class Scope;

    // The identity of a movableContentOf: where its state lives now (see MovableContent).
    struct MovableHandle {
        Scope* scope = nullptr;
        Composition* composition = nullptr;
        std::uint64_t placedPass = 0;
        std::uint64_t placedBatch = 0;   // the RecomposeAll it was last placed in
    };

    struct Group {
        enum class Kind : std::uint8_t { Slot, Node, Scope, Effect };
        Kind kind = Kind::Slot;
        std::type_index type = typeid(void);
        Key key;
        Keys keys;
        std::shared_ptr<void> slot;              // Slot: the remembered value
        RememberObserver* observer = nullptr;    // Slot: the value, when it observes remembering
        std::unique_ptr<TreeNode> node;          // Node: the node itself
        std::unique_ptr<Scope> scope;            // Node content or Scope
        // Effect: shared because effects run after apply, when groups may have moved.
        struct EffectState { kiln::Function<void()> disposer; Job job; bool started = false; };
        std::shared_ptr<EffectState> effect;
    };

    class Scope final : public Subscriber {
    public:
        Scope(Composition& composition, Scope* parent, TreeNode* hostNode, std::uint64_t groupKey = 0);
        ~Scope() override;

        bool Invalid() const { return m_Invalid; }
        int Depth() const { return m_Depth; }

    private:
        friend class Composition;
        friend class RecomposeScope;
        void OnStateChanged() override;
        void Run();
        void CollectNodes(std::vector<TreeNode*>& out) const;
        const StateObject* FindLocal(const void* local) const;

        Composition* m_Composition;    // changes when movable content moves to another composition
        Scope* m_Parent;
        TreeNode* m_Host;              // node whose children this scope's nodes are
        kiln::Function<void()> m_Content;
        Keys m_Memo;
        bool m_HasMemo = false;
        std::vector<Group> m_Groups;
        int m_Depth = 0;
        std::uint64_t m_CompositeKey = 0;
        bool m_Invalid = true;
        bool m_Disposed = false;
        bool m_AwaitingInsert = false;   // movable content waiting for the end of the batch
        std::unordered_map<const void*, std::shared_ptr<StateObject>> m_Provided;   // Local* -> State<T>
        // Computed defaults read in this scope (Local* -> T), recomputed on each read.
        std::unordered_map<const void*, std::shared_ptr<void>> m_ComputedDefaults;
        std::shared_ptr<Scope*> m_Self = std::make_shared<Scope*>(this);   // cleared on destruction
    };

    // RecomposeScope (currentRecomposeScope): invalidates the scope it was taken in, so it runs
    // again in the next recomposition. Does nothing once that scope is gone.
    class RecomposeScope {
    public:
        RecomposeScope() = default;
        explicit RecomposeScope(std::weak_ptr<Scope*> scope) : m_Scope(std::move(scope)) {}
        void Invalidate() const;
        bool operator==(const RecomposeScope& o) const { return m_Scope.lock() == o.m_Scope.lock(); }
    private:
        std::weak_ptr<Scope*> m_Scope;
    };

    // CompositionLocalContext (currentCompositionLocalContext): the locals provided at a point,
    // to provide again elsewhere (Composition::Provide(context, content)), still following
    // changes to the values where they were provided.
    struct CompositionLocalContext {
        std::unordered_map<const void*, std::shared_ptr<StateObject>> provided;
        bool operator==(const CompositionLocalContext&) const = default;
    };

    // ShouldPauseCallback: asked between steps of a paused composition whether to yield.
    using ShouldPauseCallback = kiln::Function<bool()>;

    class Composition {
    public:
        // root: the node the top-level content's nodes become children of.
        explicit Composition(TreeNode& root, Dispatcher& dispatcher = Dispatcher::Main());
        ~Composition();

        void SetContent(kiln::Function<void()> content);
        // ReusableComposition.deactivate: forgets every remembered value and effect (their
        // observers and disposers run now) but keeps the nodes. The next SetContent composes
        // anew over them, reusing each node its calls match (setContentWithReuse).
        void Deactivate();
        bool IsDeactivated() const { return m_Deactivated; }
        // Frame step 3: apply state changes, recompose, apply node changes, run effects.
        // Returns whether anything recomposed.
        bool Recompose();
        bool HasInvalidScopes() const { return !m_Pending.empty(); }

        Dispatcher& GetDispatcher() { return m_Dispatcher; }
        // currentCompositeKeyHash for a call at `key` in the current scope: stable across runs and
        // process restarts (built from call-site and explicit keys), shared by repeated calls.
        std::uint64_t CompositeKey(Key key = {}) const;
        // A new node's compositeKeyHash: its composite key, told apart from earlier nodes of the
        // same call site in this scope by their order.
        std::uint64_t NodeKeyHash(const Group& g) const;
        static Composition* Current();
        // Every live composition (the main content and measure-time ones such as lazy items);
        // the host recomposes all of them each frame.
        static bool RecomposeAll();

        // --- calls made from inside content -------------------------------------------------

        template<typename T, typename Init>
        T& Remember(Init&& init, Keys keys = {}, Key key = {}) {
            Group& g = Take(Group::Kind::Slot, typeid(T), key);
            if (!g.slot || g.keys != keys) {
                auto value = std::make_shared<T>(init());
                RememberSlot(g, value, ObserverOf(value.get()));
                g.keys = std::move(keys);
            }
            return *static_cast<T*>(g.slot.get());
        }

        // Constructs T in place from args on first call (for non-movable types such as State).
        template<typename T, typename... Args>
        T& RememberNew(Keys keys, Key key, Args&&... args) {
            Group& g = Take(Group::Kind::Slot, typeid(T), key);
            if (!g.slot || g.keys != keys) {
                auto value = std::make_shared<T>(std::forward<Args>(args)...);
                RememberSlot(g, value, ObserverOf(value.get()));
                g.keys = std::move(keys);
            }
            return *static_cast<T*>(g.slot.get());
        }

        // currentRecomposeScope
        RecomposeScope CurrentRecomposeScope() const { return RecomposeScope(m_Current ? std::weak_ptr<Scope*>(m_Current->m_Self) : std::weak_ptr<Scope*>()); }
        // currentCompositionLocalContext: every local provided above this point.
        CompositionLocalContext CurrentLocalContext() const;
        // CompositionLocalProvider(context): provides the context's locals to `content`.
        void Provide(const CompositionLocalContext& context, kiln::Function<void()> content, Key key = {});
        // ReusableContent(key): content whose remembered state starts over when `key` changes
        // (kiln composes it anew; Compose reuses the old nodes).
        void ReusableContent(Key key, const kiln::Function<void()>& content) { Section(content, key); }
        // ReusableContentHost(active): content composed while `active`; deactivating forgets its
        // remembered state (kiln disposes it; Compose keeps its nodes for reuse).
        void ReusableContentHost(bool active, const kiln::Function<void()>& content, Key key = {}) {
            Section([&] { if (active) content(); }, key);
        }
        // remember { mutableStateOf(v) }
        template<typename T>
        State<T>& RememberState(T initial, Key key = {}) { return RememberNew<State<T>>({}, key, std::move(initial)); }

        // A child scope re-run on its own when state it reads changes. With memo keys it is
        // skipped while the keys are equal and nothing it read changed.
        void Child(kiln::Function<void()> content, Key key = {}, const Keys* memo = nullptr);
        // A non-restartable group (Compose's key { } and if / else groups): its calls get their
        // own identity, but it runs only with its parent, which observes what it reads. Use it
        // where a restartable Child would outlive what its content refers to (a function that
        // returns a value, content capturing locals by reference).
        void Section(const kiln::Function<void()>& content, Key key = {});

        // Emits a node of type N: created by make() on first call, update(node) every run,
        // content composes its children.
        template<typename N>
        N& Node(const kiln::Function<std::unique_ptr<N>()>& make, const kiln::Function<void(N&)>& update,
                kiln::Function<void()> content = {}, Key key = {}) {
            Group& g = Take(Group::Kind::Node, typeid(N), key);
            if (!g.node) {
                g.node = make();
                g.scope = std::make_unique<Scope>(*this, m_Current, g.node.get(), g.key.value);
                g.node->m_CompositeKeyHash = NodeKeyHash(g);
                g.node->m_ContentScope = g.scope.get();
            } else if (g.node->m_Deactivated) {
                g.node->m_Deactivated = false;
                g.node->OnReuse();
            }
            auto& n = static_cast<N&>(*g.node);
            if (update) update(n);
            RunChild(*g.scope, std::move(content), nullptr);
            return n;
        }

        // Composes `content` as the movable content `handle`: a call site that newly calls it adopts
        // the state (and nodes) a site that stopped calling it in this recomposition left behind.
        void Movable(const std::shared_ptr<MovableHandle>& handle, kiln::Function<void()> content);

        void SideEffect(kiln::Function<void()> effect);
        void DisposableEffect(Keys keys, kiln::Function<kiln::Function<void()>()> effect, Key key = {});
        void LaunchedEffect(Keys keys, kiln::Function<Task<>()> task, Key key = {});

        template<typename T>
        const T& Read(const Local<T>& local) { return ReadFrom(m_Current, local); }

        // The value of `local` at `scope` (a node's content scope, say).
        template<typename T>
        static const T& ReadFrom(Scope* scope, const Local<T>& local) {
            // Up this composition's scopes, then on through the parent context of measure-time
            // compositions (subcompose inherits the CompositionLocals where it was composed).
            for (Scope* start = scope; start;) {
                Scope* last = nullptr;
                for (Scope* s = start; s; s = s->m_Parent) {
                    if (const StateObject* p = s->FindLocal(&local))
                        return static_cast<const State<T, NeverEqual>*>(p)->Get();
                    last = s;
                }
                start = last ? last->m_Composition->m_ParentContext : nullptr;
            }
            if (local.HasComputedDefault()) return ComputedDefault(scope, local);
            return local.Default();
        }

        // The value provided above this point, or null where only the default applies.
        template<typename T>
        const T* ReadIfProvided(const Local<T>& local) {
            for (Scope* start = m_Current; start;) {
                Scope* last = nullptr;
                for (Scope* s = start; s; s = s->m_Parent) {
                    if (const StateObject* p = s->FindLocal(&local))
                        return &static_cast<const State<T, NeverEqual>*>(p)->Get();
                    last = s;
                }
                start = last ? last->m_Composition->m_ParentContext : nullptr;
            }
            return nullptr;
        }

        // For compositions created during measure (SubcomposeLayout, lazy items): the scope whose
        // Locals the content sees. It must outlive this composition.
        void SetParentContext(Scope* scope) {
            m_ParentContext = scope;
            // Taken now: the scope can be gone by the time this composition is disposed.
            m_InheritedGate = scope && scope->m_Composition ? scope->m_Composition->CurrentFrameGate() : nullptr;
        }
        // Jobs this composition launches (LaunchedEffect, RememberCoroutineScope) take this gate,
        // or the one of the composition it is nested in (a dialog's content until its window shows).
        void SetFrameGate(std::shared_ptr<const FrameGate> gate) { m_FrameGate = std::move(gate); }
        std::shared_ptr<const FrameGate> CurrentFrameGate() const {
            return m_FrameGate ? m_FrameGate : m_InheritedGate;
        }
        Scope* CurrentScope() const { return m_Current; }

        template<typename T>
        void Provide(const Local<T>& local, T value, kiln::Function<void()> content, Key key = {}) {
            using Cell = State<T, NeverEqual>;
            Group& slot = Take(Group::Kind::Slot, typeid(Cell), key);
            if (!slot.slot) slot.slot = std::make_shared<Cell>(value);
            std::shared_ptr<Cell> cell(slot.slot, static_cast<Cell*>(slot.slot.get()));
            // Equal values must not notify readers; NeverEqual is only the storage type.
            if (!(cell->Peek() == value)) cell->Set(std::move(value));
            Group& g = Take(Group::Kind::Scope, typeid(Local<T>), key);
            if (!g.scope) g.scope = std::make_unique<Scope>(*this, m_Current, m_Current->m_Host, g.key.value);
            g.scope->m_Provided[&local] = std::move(cell);
            RunChild(*g.scope, std::move(content), nullptr);
        }

    private:
        template<typename T>
        static RememberObserver* ObserverOf(T* v) {
            if constexpr (std::is_base_of_v<RememberObserver, T>) return static_cast<RememberObserver*>(v);
            else return nullptr;
        }
        // Stores a newly remembered value, forgetting the one it replaces.
        void RememberSlot(Group& g, std::shared_ptr<void> value, RememberObserver* observer);
        template<typename T>
        static const T& ComputedDefault(Scope* scope, const Local<T>& local);

        friend class Scope;
        friend class RecomposeScope;
        Group& Take(Group::Kind kind, std::type_index type, Key key);
        void RunChild(Scope& scope, kiln::Function<void()> content, const Keys* memo);
        void RunScope(Scope& scope);
        void Dispose(Group& g);
        void DeactivateScope(Scope& scope);
        void Park(Group& g);
        void Reparent(Scope& scope, Scope* parent, TreeNode* oldHost, TreeNode* newHost);
        void Unpend(Scope& scope);
        void Repend(Scope& scope);
        void DisposeParked();
        // Movable content moving between compositions (see RecomposeAll).
        static Composition* ParkedOwner(MovableHandle* handle, Composition* self);
        void Retarget(Scope& scope);
        void InsertAwaiting(const std::shared_ptr<MovableHandle>& handle, Scope& placeholder);
        void Invalidate(Scope& scope);
        void ApplyNodeChanges();
        void RunEffects();

        TreeNode& m_Root;
        Dispatcher& m_Dispatcher;
        std::unique_ptr<Scope> m_RootScope;

        // Per-run matching state for the scope currently running.
        struct Frame {
            Scope* scope;
            std::vector<Group> old;
            std::vector<bool> used;
            std::size_t cursor = 0;
        };
        std::vector<Frame> m_Stack;
        Scope* m_Current = nullptr;
        Scope* m_ParentContext = nullptr;
        std::shared_ptr<const FrameGate> m_FrameGate, m_InheritedGate;

        std::vector<Scope*> m_Invalid;
        std::unordered_set<Scope*> m_Pending;
        std::unordered_set<Scope*> m_ChildrenDirty;   // top content scope of each touched host
        std::vector<std::unique_ptr<TreeNode>> m_DeadNodes;
        std::vector<kiln::Function<void()>> m_Effects;     // run after apply, in order
        std::vector<kiln::Function<void()>> m_Disposers;   // run after apply, before effects
        // Remembered values of disposed groups, freed after the disposers ran (a disposer may
        // still use what was remembered beside it).
        std::vector<std::shared_ptr<void>> m_DeadSlots;
        // Movable content dropped in this recomposition, adoptable until it ends.
        std::unordered_map<MovableHandle*, Group> m_Parked;
        std::uint64_t m_PassId = 1;
        bool m_Destroying = false;
        bool m_Deactivated = false;
    };


    // CompositionLocalAccessorScope: reads other locals' values where a computed default is read.
    struct LocalAccessorScope {
        Scope* scope = nullptr;
        template<typename T> const T& CurrentValue(const Local<T>& local) const { return Composition::ReadFrom(scope, local); }
    };

    template<typename T>
    const T& Composition::ComputedDefault(Scope* scope, const Local<T>& local) {
        static thread_local std::unordered_map<const void*, std::shared_ptr<void>> outside;   // read outside any scope
        T value = local.ComputeDefault(LocalAccessorScope{ scope });
        std::shared_ptr<void>& cell = scope ? scope->m_ComputedDefaults[&local] : outside[&local];
        if (!cell) cell = std::make_shared<T>(std::move(value));
        else *static_cast<T*>(cell.get()) = std::move(value);
        return *static_cast<const T*>(cell.get());
    }

    // produceState: state whose value `producer` sets from a task launched with the keys
    // (restarted when they change, cancelled when the call leaves).
    template<typename T>
    struct ProduceStateScope {
        State<T>& state;
        void Set(T v) const { state.Set(std::move(v)); }
        const T& Get() const { return state.Peek(); }
    };
    template<typename T>
    State<T>& ProduceState(T initial, Keys keys, kiln::Function<Task<>(ProduceStateScope<T>)> producer, Key key = {}) {
        Composition* c = Composition::Current();
        State<T>& state = c->RememberState(std::move(initial), key);
        State<T>* ptr = &state;
        c->LaunchedEffect(std::move(keys), [ptr, producer = std::move(producer)]() -> Task<> { return producer(ProduceStateScope<T>{ *ptr }); }, key);
        return state;
    }

    // movableContentOf: composable content whose state and nodes move with it when the call
    // moves to another place in the same recomposition (one site stops calling it, another
    // starts). Remember the result; each call composes it at the call site.
    template<typename... P>
    class MovableContent {
    public:
        explicit MovableContent(kiln::Function<void(P...)> content)
            : m_Content(std::make_shared<kiln::Function<void(P...)>>(std::move(content))), m_Handle(std::make_shared<MovableHandle>()) {}
        void operator()(P... args) const {
            auto fn = m_Content;
            Composition::Current()->Movable(m_Handle, [fn, args...] { (*fn)(args...); });
        }
        bool operator==(const MovableContent& o) const { return m_Handle == o.m_Handle; }
    private:
        std::shared_ptr<kiln::Function<void(P...)>> m_Content;
        std::shared_ptr<MovableHandle> m_Handle;
    };

    template<typename... P>
    MovableContent<P...> MovableContentOf(kiln::Function<void(P...)> content) { return MovableContent<P...>(std::move(content)); }

}
