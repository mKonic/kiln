#pragma once

// rememberDecoratedNavEntries, the SaveableStateHolder decorator and NavBackStack
// (navigation3-runtime DecoratedNavEntries.kt, SaveableStateHolderNavEntryDecorator.kt,
// NavBackStack.kt, RememberNavBackStack.kt).

#include "kiln/navigation3/NavEntry.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/runtime/State.h"

#include <algorithm>
#include <initializer_list>
#include <set>

namespace kiln::navigation3 {

    namespace detail {
        inline std::size_t HashKeys(const std::vector<std::string>& keys) {
            std::size_t h = keys.size();
            for (const auto& k : keys) h = h * 1000003u ^ std::hash<std::string>{}(k);
            return h;
        }

        template<typename T>
        struct DecorationState {
            std::set<std::string> keysInBackStack, keysInComposition;
            std::vector<NavEntryDecorator<T>> latestDecorators;
            // The latest back stack's content keys (not the entries: their content holds this).
            std::vector<std::string> latestKeys;
        };

        // One entry's decoration: rebuilt when the entry or the decorators change, so the
        // decorated entry compares equal across recompositions otherwise.
        template<typename T>
        struct DecoratedEntry {
            std::optional<NavEntry<T>> source, decorated;
            std::vector<NavEntryDecorator<T>> decorators;
        };

        // decorateEntry: the entry's content tracks whether it is composed (for onPop after a
        // pop animation) and runs inside the decorators, the first one outermost.
        template<typename T>
        NavEntry<T> DecorateEntry(const NavEntry<T>& entry, const std::vector<NavEntryDecorator<T>>& decorators,
                                  const std::shared_ptr<DecorationState<T>>& state) {
            auto& c = *rt::Composition::Current();
            const std::string contentKey = entry.ContentKey();
            auto& holder = c.Remember<std::shared_ptr<DecoratedEntry<T>>>([] { return std::make_shared<DecoratedEntry<T>>(); }, {}, rt::Key(contentKey));
            if (holder->decorated && holder->source && *holder->source == entry && holder->decorators == decorators) return *holder->decorated;
            holder->source = entry;
            holder->decorators = decorators;
            std::vector<NavEntryDecorator<T>> distinct;
            for (const auto& d : decorators)
                if (std::find(distinct.begin(), distinct.end(), d) == distinct.end()) distinct.push_back(d);
            holder->decorated = NavEntry<T>(entry, [entry, distinct, state, contentKey](const T&) {
                auto& cc = *rt::Composition::Current();
                cc.DisposableEffect(rt::Keys(contentKey), [state, contentKey] {
                    state->keysInComposition.insert(contentKey);
                    return std::function<void()>([state, contentKey] {
                        const bool notInComposition = state->keysInComposition.erase(contentKey) > 0;
                        const bool popped = !state->keysInBackStack.contains(contentKey);
                        if (popped && notInComposition)
                            for (auto it = state->latestDecorators.rbegin(); it != state->latestDecorators.rend(); ++it) it->OnPop(contentKey);
                    });
                });
                NavEntry<T> wrapped = entry;
                for (auto it = distinct.rbegin(); it != distinct.rend(); ++it) {
                    const NavEntryDecorator<T> decorator = *it;
                    const NavEntry<T> inner = wrapped;
                    wrapped = NavEntry<T>(inner, [decorator, inner](const T&) { decorator.Decorate(inner); });
                }
                wrapped.Content();
            });
            return *holder->decorated;
        }

        // PrepareBackStack: entries popped without being composed (or done animating) get
        // their onPop here.
        template<typename T>
        void PrepareBackStack(const NavEntries<T>& entries, const std::shared_ptr<DecorationState<T>>& state) {
            auto& c = *rt::Composition::Current();
            std::vector<std::string> keys;
            for (const auto& e : entries) keys.push_back(e.ContentKey());
            const std::size_t listHash = HashKeys(keys);
            for (const auto& e : entries) {
                const std::string contentKey = e.ContentKey();
                state->keysInBackStack.insert(contentKey);
                c.DisposableEffect(rt::Keys(contentKey, listHash), [state, contentKey] {
                    return std::function<void()>([state, contentKey] {
                        const bool inLatest = std::find(state->latestKeys.begin(), state->latestKeys.end(), contentKey) != state->latestKeys.end();
                        const bool popped = !inLatest && state->keysInBackStack.erase(contentKey) > 0;
                        if (popped && !state->keysInComposition.contains(contentKey))
                            for (auto it = state->latestDecorators.rbegin(); it != state->latestDecorators.rend(); ++it) it->OnPop(contentKey);
                    });
                }, rt::Key(contentKey));
            }
        }
    }

    // rememberDecoratedNavEntries(entries, entryDecorators): the entries wrapped in the
    // decorators (these first, then the ones the entries already had).
    template<typename T>
    NavEntries<T> RememberDecoratedNavEntries(const NavEntries<T>& entries, const std::vector<NavEntryDecorator<T>>& entryDecorators = {},
                                              rt::Key key = {}) {
        // Returns a value, so it is not a restartable scope of its own (like Compose).
        auto& c = *rt::Composition::Current();
        NavEntries<T> decorated;
        auto& state = c.Remember<std::shared_ptr<detail::DecorationState<T>>>([] { return std::make_shared<detail::DecorationState<T>>(); }, {}, key);
        state->latestDecorators = entryDecorators;
        for (const auto& e : entries) decorated.push_back(detail::DecorateEntry(e, entryDecorators, state));
        state->latestKeys.clear();
        for (const auto& e : decorated) state->latestKeys.push_back(e.ContentKey());
        detail::PrepareBackStack(decorated, state);
        return decorated;
    }

    // rememberDecoratedNavEntries(backStack, entryDecorators, entryProvider): entries are made
    // again only when the keys change.
    template<typename T>
    NavEntries<T> RememberDecoratedNavEntries(const std::vector<T>& backStack, const std::vector<NavEntryDecorator<T>>& entryDecorators,
                                              const std::function<NavEntry<T>(const T&)>& entryProvider, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        struct Cache { std::vector<T> keys; NavEntries<T> entries; bool valid = false; };
        auto& cache = c.Remember<std::shared_ptr<Cache>>([] { return std::make_shared<Cache>(); }, {}, key);
        if (!cache->valid || !(cache->keys == backStack)) {
            cache->keys = backStack;
            cache->entries.clear();
            for (const auto& k : backStack) cache->entries.push_back(entryProvider(k));
            cache->valid = true;
        }
        return RememberDecoratedNavEntries(cache->entries, entryDecorators, key);
    }

    // SaveableStateHolderNavEntryDecorator: rememberSaveable inside an entry saves into the
    // holder under the entry's content key, kept while the entry is on the back stack.
    template<typename T>
    NavEntryDecorator<T> SaveableStateHolderNavEntryDecorator(rt::SaveableStateHolder& holder) {
        auto* h = &holder;
        return NavEntryDecorator<T>([h](const std::string& contentKey) { h->RemoveState(contentKey); },
                                    [h](const NavEntry<T>& entry) {
                                        rt::SaveableStateProvider(*h, entry.ContentKey(), [entry] { entry.Content(); });
                                    });
    }

    // rememberSaveableStateHolderNavEntryDecorator(saveableStateHolder = rememberSaveableStateHolder())
    template<typename T>
    NavEntryDecorator<T> RememberSaveableStateHolderNavEntryDecorator(rt::SaveableStateHolder* holder = nullptr, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        rt::SaveableStateHolder& h = holder ? *holder : rt::RememberSaveableStateHolder(key);
        auto& decorator = c.Remember<std::shared_ptr<NavEntryDecorator<T>>>(
            [&] { return std::make_shared<NavEntryDecorator<T>>(SaveableStateHolderNavEntryDecorator<T>(h)); }, rt::Keys(&h), key);
        return *decorator;
    }

    // NavBackStack: an observable list of keys.
    template<typename T>
    class NavBackStack {
    public:
        NavBackStack() = default;
        NavBackStack(std::initializer_list<T> keys) { for (const auto& k : keys) m_Keys.Add(k); }
        explicit NavBackStack(const std::vector<T>& keys) { for (const auto& k : keys) m_Keys.Add(k); }
        std::vector<T> ToList() const { return m_Keys.Get(); }
        std::size_t Size() const { return m_Keys.Size(); }
        bool Empty() const { return m_Keys.Size() == 0; }
        const T& operator[](std::size_t i) const { return m_Keys[i]; }
        const T& Last() const { return m_Keys[m_Keys.Size() - 1]; }
        std::optional<T> LastOrNull() const { return Empty() ? std::nullopt : std::optional<T>(Last()); }
        void Add(T key) { m_Keys.Add(std::move(key)); }
        void RemoveAt(std::size_t i) { m_Keys.RemoveAt(i); }
        // removeLastOrNull
        std::optional<T> RemoveLast() {
            if (Empty()) return std::nullopt;
            T last = Last();
            m_Keys.RemoveAt(m_Keys.Size() - 1);
            return last;
        }
        void Clear() { m_Keys.Clear(); }
        void Set(std::vector<T> keys) { m_Keys.Update([&](std::vector<T>& v) { v = std::move(keys); }); }

    private:
        rt::StateList<T> m_Keys;
    };

    // rememberNavBackStack(elements): a back stack saved (each key with keySaver) and restored.
    template<typename T>
    NavBackStack<T>& RememberNavBackStack(rt::Saver<T> keySaver, std::vector<T> elements, rt::Key key = {}) {
        using Ptr = std::shared_ptr<NavBackStack<T>>;
        rt::Saver<Ptr> saver{
            [keySaver](const Ptr& stack) {
                rt::SavedValue::List list;
                for (const auto& k : stack->ToList()) list.push_back(keySaver.save(k));
                return rt::SavedValue(std::move(list));
            },
            [keySaver](const rt::SavedValue& s) -> std::optional<Ptr> {
                const auto* list = std::get_if<rt::SavedValue::List>(&s.v);
                if (!list) return std::nullopt;
                std::vector<T> keys;
                for (const auto& v : *list) {
                    auto k = keySaver.restore(v);
                    if (!k) return std::nullopt;
                    keys.push_back(std::move(*k));
                }
                return std::make_shared<NavBackStack<T>>(keys);
            },
        };
        return *rt::RememberSaveable<Ptr>([&] { return std::make_shared<NavBackStack<T>>(elements); }, saver, {}, key);
    }

}
