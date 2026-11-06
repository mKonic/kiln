#include "kiln/runtime/Composition.h"

#include <bit>

#include <algorithm>

namespace kiln::rt {

    namespace {
        thread_local Composition* t_Current = nullptr;
        thread_local std::vector<Composition*> t_All;
        struct DisposableTag {};
        struct LaunchedTag {};
        struct SideEffectTag {};
    }

    // --- Scope --------------------------------------------------------------------------------

    namespace {
        // currentCompositeKeyHash: rotate the parent's hash and mix in the group key.
        std::uint64_t Compound(std::uint64_t parent, std::uint64_t key) { return std::rotl(parent, 3) ^ key; }
    }

    Scope::Scope(Composition& composition, Scope* parent, TreeNode* hostNode, std::uint64_t groupKey)
        : m_Composition(composition), m_Parent(parent), m_Host(hostNode),
          m_Depth(parent ? parent->m_Depth + 1 : 0),
          m_CompositeKey(parent ? Compound(parent->m_CompositeKey, groupKey) : 0) {}

    std::uint64_t Composition::CompositeKey(Key key) const {
        return Compound(m_Current ? m_Current->m_CompositeKey : 0, key.value);
    }

    Scope::~Scope() {
        m_Disposed = true;
        m_Composition.m_Pending.erase(this);
        m_Composition.m_ChildrenDirty.erase(this);
    }

    void Scope::OnStateChanged() { m_Composition.Invalidate(*this); }

    void Scope::CollectNodes(std::vector<TreeNode*>& out) const {
        for (const Group& g : m_Groups) {
            if (g.kind == Group::Kind::Node) out.push_back(g.node.get());
            else if (g.kind == Group::Kind::Scope && g.scope && g.scope->m_Host == m_Host) g.scope->CollectNodes(out);
        }
    }

    const StateObject* Scope::FindLocal(const void* local) const {
        auto it = m_Provided.find(local);
        return it == m_Provided.end() ? nullptr : it->second;
    }

    // --- Composition --------------------------------------------------------------------------

    Composition::Composition(TreeNode& root, Dispatcher& dispatcher) : m_Root(root), m_Dispatcher(dispatcher) {
        t_All.push_back(this);
    }

    bool Composition::RecomposeAll() {
        bool any = false;
        // Recomposing one composition may create or destroy others (lazy items): iterate a copy
        // and skip ones that are gone.
        auto all = t_All;
        for (Composition* c : all)
            if (std::find(t_All.begin(), t_All.end(), c) != t_All.end()) any |= c->Recompose();
        return any;
    }

    Composition::~Composition() {
        std::erase(t_All, this);
        if (m_RootScope) {
            for (Group& g : m_RootScope->m_Groups) Dispose(g);
            m_RootScope->m_Groups.clear();
            m_RootScope.reset();
        }
        // Unhook the nodes from the host tree before freeing them; the root outlives us.
        m_Root.SetChildren({});
        m_DeadNodes.clear();
        for (auto& d : m_Disposers) d();
    }

    Composition* Composition::Current() { return t_Current; }

    void Composition::SetContent(std::function<void()> content) {
        if (!m_RootScope) m_RootScope = std::make_unique<Scope>(*this, nullptr, &m_Root);
        m_RootScope->m_Content = std::move(content);
        Invalidate(*m_RootScope);
    }

    void Composition::Invalidate(Scope& scope) {
        if (scope.m_Disposed) return;
        scope.m_Invalid = true;
        if (m_Pending.insert(&scope).second) {
            m_Invalid.push_back(&scope);
            if (m_Dispatcher.requestFrame) m_Dispatcher.requestFrame();
        }
    }

    Group& Composition::Take(Group::Kind kind, std::type_index type, Key key) {
        Frame& f = m_Stack.back();
        auto matches = [&](std::size_t j) {
            const Group& o = f.old[j];
            return !f.used[j] && o.kind == kind && o.type == type && o.key.value == key.value;
        };
        std::size_t found = f.old.size();
        for (std::size_t j = f.cursor; j < f.old.size(); ++j)
            if (matches(j)) { found = j; break; }
        if (found == f.old.size())
            for (std::size_t j = 0; j < f.cursor && j < f.old.size(); ++j)
                if (matches(j)) { found = j; break; }

        Scope& scope = *f.scope;
        if (found < f.old.size()) {
            f.used[found] = true;
            f.cursor = found + 1;
            scope.m_Groups.push_back(std::move(f.old[found]));
        } else {
            Group g;
            g.kind = kind;
            g.type = type;
            g.key = key;
            scope.m_Groups.push_back(std::move(g));
        }
        return scope.m_Groups.back();
    }

    void Composition::RunScope(Scope& scope) {
        Frame frame;
        frame.scope = &scope;
        frame.old = std::move(scope.m_Groups);
        scope.m_Groups.clear();
        scope.m_Groups.reserve(frame.old.size());
        frame.used.assign(frame.old.size(), false);
        m_Stack.push_back(std::move(frame));

        Scope* outer = m_Current;
        Composition* outerComposition = t_Current;
        m_Current = &scope;
        t_Current = this;
        struct Restore {
            Composition& c; Scope* s; Composition* oc;
            ~Restore() { c.m_Current = s; t_Current = oc; }
        } restore{ *this, outer, outerComposition };

        scope.m_Invalid = false;
        m_Pending.erase(&scope);
        if (scope.m_Content) Observation::Track(scope, scope.m_Content);

        Frame done = std::move(m_Stack.back());
        m_Stack.pop_back();
        for (std::size_t j = 0; j < done.old.size(); ++j)
            if (!done.used[j]) Dispose(done.old[j]);

        // The host node's child list may have changed; find the top scope that composes it.
        Scope* top = &scope;
        while (top->m_Parent && top->m_Parent->m_Host == scope.m_Host) top = top->m_Parent;
        m_ChildrenDirty.insert(top);
    }

    void Composition::RunChild(Scope& scope, std::function<void()> content, const Keys* memo) {
        if (memo && scope.m_HasMemo && *memo == scope.m_Memo && !scope.m_Invalid) return;   // skipped
        scope.m_Content = std::move(content);
        if (memo) { scope.m_Memo = *memo; scope.m_HasMemo = true; }
        RunScope(scope);
    }

    void Composition::Child(std::function<void()> content, Key key, const Keys* memo) {
        Group& g = Take(Group::Kind::Scope, typeid(Scope), key);
        if (!g.scope) g.scope = std::make_unique<Scope>(*this, m_Current, m_Current->m_Host, g.key.value);
        RunChild(*g.scope, std::move(content), memo);
    }

    void Composition::Dispose(Group& g) {
        if (g.scope) {
            for (Group& child : g.scope->m_Groups) Dispose(child);
            g.scope->m_Groups.clear();
            g.scope.reset();
        }
        if (g.node) m_DeadNodes.push_back(std::move(g.node));   // destroyed after re-childing
        if (g.effect) {
            g.effect->job.Cancel();
            if (g.effect->disposer) m_Disposers.push_back(std::move(g.effect->disposer));
            g.effect.reset();
        }
        g.slot.reset();
    }

    void Composition::SideEffect(std::function<void()> effect) {
        m_Effects.push_back(std::move(effect));
    }

    void Composition::DisposableEffect(Keys keys, std::function<std::function<void()>()> effect, Key key) {
        Group& g = Take(Group::Kind::Effect, typeid(DisposableTag), key);
        if (!g.effect) g.effect = std::make_shared<Group::EffectState>();
        if (g.effect->started && g.keys == keys) return;
        g.keys = std::move(keys);
        g.effect->started = true;
        auto state = g.effect;
        m_Effects.push_back([state, effect = std::move(effect)] {
            if (state->disposer) { auto d = std::move(state->disposer); state->disposer = nullptr; d(); }
            state->disposer = effect();
        });
    }

    void Composition::LaunchedEffect(Keys keys, std::function<Task<>()> task, Key key) {
        Group& g = Take(Group::Kind::Effect, typeid(LaunchedTag), key);
        if (!g.effect) g.effect = std::make_shared<Group::EffectState>();
        if (g.effect->started && g.keys == keys) return;
        g.keys = std::move(keys);
        g.effect->started = true;
        auto state = g.effect;
        Dispatcher* dispatcher = &m_Dispatcher;
        m_Effects.push_back([state, dispatcher, task = std::move(task)] {
            state->job.Cancel();
            state->job = Launch(*dispatcher, task());
        });
    }

    void Composition::ApplyNodeChanges() {
        auto dirty = std::move(m_ChildrenDirty);
        m_ChildrenDirty.clear();
        std::vector<TreeNode*> children;
        for (Scope* top : dirty) {
            children.clear();
            top->CollectNodes(children);
            top->m_Host->SetChildren(children);
        }
        m_DeadNodes.clear();
    }

    void Composition::RunEffects() {
        // Disposers first (removed or restarted effects), then new effects, in call order.
        // Effects run untracked: their reads do not subscribe anything.
        while (!m_Disposers.empty() || !m_Effects.empty()) {
            auto disposers = std::move(m_Disposers); m_Disposers.clear();
            auto effects = std::move(m_Effects); m_Effects.clear();
            Observation::Untracked([&] {
                for (auto& d : disposers) d();
                for (auto& e : effects) e();
            });
        }
    }

    bool Composition::Recompose() {
        bool recomposed = false;
        // Apply until stable: a recomposition or a derived value can write state that other
        // scopes read. The bound stops a scope that writes what it reads from looping forever.
        for (int pass = 0; pass < 32; ++pass) {
            // A derived value that changed announces itself as a new write, picked up by the
            // next apply; keep going while changes are still flowing.
            const bool changed = Observation::ApplyChanges();
            if (m_Pending.empty()) {
                if (changed) continue;
                break;
            }
            auto list = std::move(m_Invalid);
            m_Invalid.clear();
            std::sort(list.begin(), list.end(), [](Scope* a, Scope* b) { return a->m_Depth < b->m_Depth; });
            for (Scope* s : list) {
                // Pending membership is checked before touching s: a parent that re-ran in this
                // pass may have disposed it (~Scope erases itself).
                if (!m_Pending.count(s)) continue;
                if (s->m_Invalid) { RunScope(*s); recomposed = true; }
                else m_Pending.erase(s);
            }
        }
        ApplyNodeChanges();
        RunEffects();
        return recomposed;
    }

}
