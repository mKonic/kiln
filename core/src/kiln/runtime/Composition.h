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
#include "kiln/runtime/Task.h"

#include <any>
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

    template<typename T>
    class Local {
    public:
        explicit Local(T defaultValue) : m_Default(std::move(defaultValue)) {}
        const T& Default() const { return m_Default; }
    private:
        T m_Default;
    };

    class Composition;
    class Scope;

    struct Group {
        enum class Kind : std::uint8_t { Slot, Node, Scope, Effect };
        Kind kind = Kind::Slot;
        std::type_index type = typeid(void);
        Key key;
        Keys keys;
        std::shared_ptr<void> slot;              // Slot: the remembered value
        std::unique_ptr<TreeNode> node;          // Node: the node itself
        std::unique_ptr<Scope> scope;            // Node content or Scope
        // Effect: shared because effects run after apply, when groups may have moved.
        struct EffectState { std::function<void()> disposer; Job job; bool started = false; };
        std::shared_ptr<EffectState> effect;
    };

    class Scope final : public Subscriber {
    public:
        Scope(Composition& composition, Scope* parent, TreeNode* hostNode);
        ~Scope() override;

        bool Invalid() const { return m_Invalid; }
        int Depth() const { return m_Depth; }

    private:
        friend class Composition;
        void OnStateChanged() override;
        void Run();
        void CollectNodes(std::vector<TreeNode*>& out) const;
        const StateObject* FindLocal(const void* local) const;

        Composition& m_Composition;
        Scope* m_Parent;
        TreeNode* m_Host;              // node whose children this scope's nodes are
        std::function<void()> m_Content;
        Keys m_Memo;
        bool m_HasMemo = false;
        std::vector<Group> m_Groups;
        int m_Depth = 0;
        bool m_Invalid = true;
        bool m_Disposed = false;
        std::unordered_map<const void*, StateObject*> m_Provided;   // Local* -> State<T>*
    };

    class Composition {
    public:
        // root: the node the top-level content's nodes become children of.
        explicit Composition(TreeNode& root, Dispatcher& dispatcher = Dispatcher::Main());
        ~Composition();

        void SetContent(std::function<void()> content);
        // Frame step 3: apply state changes, recompose, apply node changes, run effects.
        // Returns whether anything recomposed.
        bool Recompose();
        bool HasInvalidScopes() const { return !m_Pending.empty(); }

        Dispatcher& GetDispatcher() { return m_Dispatcher; }
        static Composition* Current();

        // --- calls made from inside content -------------------------------------------------

        template<typename T, typename Init>
        T& Remember(Init&& init, Keys keys = {}, Key key = {}) {
            Group& g = Take(Group::Kind::Slot, typeid(T), key);
            if (!g.slot || g.keys != keys) {
                g.slot = std::make_shared<T>(init());
                g.keys = std::move(keys);
            }
            return *static_cast<T*>(g.slot.get());
        }

        // Constructs T in place from args on first call (for non-movable types such as State).
        template<typename T, typename... Args>
        T& RememberNew(Keys keys, Key key, Args&&... args) {
            Group& g = Take(Group::Kind::Slot, typeid(T), key);
            if (!g.slot || g.keys != keys) {
                g.slot = std::make_shared<T>(std::forward<Args>(args)...);
                g.keys = std::move(keys);
            }
            return *static_cast<T*>(g.slot.get());
        }
        // remember { mutableStateOf(v) }
        template<typename T>
        State<T>& RememberState(T initial, Key key = {}) { return RememberNew<State<T>>({}, key, std::move(initial)); }

        // A child scope re-run on its own when state it reads changes. With memo keys it is
        // skipped while the keys are equal and nothing it read changed.
        void Child(std::function<void()> content, Key key = {}, const Keys* memo = nullptr);

        // Emits a node of type N: created by make() on first call, update(node) every run,
        // content composes its children.
        template<typename N>
        N& Node(const std::function<std::unique_ptr<N>()>& make, const std::function<void(N&)>& update,
                std::function<void()> content = {}, Key key = {}) {
            Group& g = Take(Group::Kind::Node, typeid(N), key);
            if (!g.node) {
                g.node = make();
                g.scope = std::make_unique<Scope>(*this, m_Current, g.node.get());
            }
            auto& n = static_cast<N&>(*g.node);
            if (update) update(n);
            RunChild(*g.scope, std::move(content), nullptr);
            return n;
        }

        void SideEffect(std::function<void()> effect);
        void DisposableEffect(Keys keys, std::function<std::function<void()>()> effect, Key key = {});
        void LaunchedEffect(Keys keys, std::function<Task<>()> task, Key key = {});

        template<typename T>
        const T& Read(const Local<T>& local) {
            for (Scope* s = m_Current; s; s = s->m_Parent)
                if (const StateObject* p = s->FindLocal(&local))
                    return static_cast<const State<T, NeverEqual>*>(p)->Get();
            return local.Default();
        }

        template<typename T>
        void Provide(const Local<T>& local, T value, std::function<void()> content, Key key = {}) {
            auto& cell = RememberNew<State<T, NeverEqual>>({}, key, value);
            // Equal values must not notify readers; NeverEqual is only the storage type.
            if (!(cell.Peek() == value)) cell.Set(std::move(value));
            Group& g = Take(Group::Kind::Scope, typeid(Local<T>), key);
            if (!g.scope) g.scope = std::make_unique<Scope>(*this, m_Current, m_Current->m_Host);
            g.scope->m_Provided[&local] = &cell;
            RunChild(*g.scope, std::move(content), nullptr);
        }

    private:
        friend class Scope;
        Group& Take(Group::Kind kind, std::type_index type, Key key);
        void RunChild(Scope& scope, std::function<void()> content, const Keys* memo);
        void RunScope(Scope& scope);
        void Dispose(Group& g);
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

        std::vector<Scope*> m_Invalid;
        std::unordered_set<Scope*> m_Pending;
        std::unordered_set<Scope*> m_ChildrenDirty;   // top content scope of each touched host
        std::vector<std::unique_ptr<TreeNode>> m_DeadNodes;
        std::vector<std::function<void()>> m_Effects;     // run after apply, in order
        std::vector<std::function<void()>> m_Disposers;   // run after apply, before effects
    };

}
