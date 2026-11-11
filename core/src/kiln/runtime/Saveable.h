#pragma once

// Saveable state (runtime-saveable; spec 14 req. 6). rememberSaveable values register providers
// with the root's SaveableStateRegistry under their composite key; the host serialises
// PerformSave() into the Activity bundle and hands the bytes back on re-creation, where values
// are consumed in registration order (several providers may share a key, e.g. in a loop).

#include "kiln/runtime/Composition.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace kiln::rt {

    // What a Bundle would hold for us: null, bool, integer, float, string, or a list of those.
    struct SavedValue {
        using List = std::vector<SavedValue>;
        std::variant<std::monostate, bool, std::int64_t, double, std::string, List> v;

        SavedValue() = default;
        SavedValue(bool b) : v(b) {}
        template<typename I> requires(std::is_integral_v<I> && !std::is_same_v<I, bool>)
        SavedValue(I i) : v(std::int64_t(i)) {}
        template<typename F> requires std::is_floating_point_v<F>
        SavedValue(F f) : v(double(f)) {}
        SavedValue(std::string s) : v(std::move(s)) {}
        SavedValue(const char* s) : v(std::string(s)) {}
        SavedValue(List l) : v(std::move(l)) {}

        bool IsNull() const { return std::holds_alternative<std::monostate>(v); }
        bool operator==(const SavedValue&) const = default;
    };

    // Saver: turns T into a SavedValue and back (Saver.kt). Restore returns nullopt to fall back
    // to the initialiser.
    template<typename T>
    struct Saver {
        std::function<SavedValue(const T&)> save;
        std::function<std::optional<T>(const SavedValue&)> restore;
    };

    // autoSaver for the types a SavedValue holds directly.
    template<typename T>
    Saver<T> AutoSaver() {
        return {
            [](const T& value) { return SavedValue(value); },
            [](const SavedValue& s) -> std::optional<T> {
                if constexpr (std::is_same_v<T, bool>) {
                    if (auto* b = std::get_if<bool>(&s.v)) return *b;
                } else if constexpr (std::is_integral_v<T>) {
                    if (auto* i = std::get_if<std::int64_t>(&s.v)) return T(*i);
                } else if constexpr (std::is_floating_point_v<T>) {
                    if (auto* d = std::get_if<double>(&s.v)) return T(*d);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    if (auto* str = std::get_if<std::string>(&s.v)) return *str;
                } else {
                    static_assert(sizeof(T) == 0, "no AutoSaver for this type: pass a Saver");
                }
                return std::nullopt;
            },
        };
    }

    // listSaver: T as a list of saveable values.
    template<typename T>
    Saver<T> ListSaver(std::function<SavedValue::List(const T&)> save, std::function<std::optional<T>(const SavedValue::List&)> restore) {
        return {
            [save = std::move(save)](const T& v) { return SavedValue(save(v)); },
            [restore = std::move(restore)](const SavedValue& s) -> std::optional<T> {
                const auto* l = std::get_if<SavedValue::List>(&s.v);
                if (!l) return std::nullopt;
                return restore(*l);
            },
        };
    }
    // mapSaver: T as named saveable values (kept as [key, value] pairs).
    template<typename T>
    Saver<T> MapSaver(std::function<std::map<std::string, SavedValue>(const T&)> save,
                      std::function<std::optional<T>(const std::map<std::string, SavedValue>&)> restore) {
        return {
            [save = std::move(save)](const T& v) {
                SavedValue::List pairs;
                for (auto& [k, value] : save(v)) pairs.emplace_back(SavedValue::List{ SavedValue(k), value });
                return SavedValue(std::move(pairs));
            },
            [restore = std::move(restore)](const SavedValue& s) -> std::optional<T> {
                const auto* l = std::get_if<SavedValue::List>(&s.v);
                if (!l) return std::nullopt;
                std::map<std::string, SavedValue> m;
                for (const auto& item : *l) {
                    const auto* pair = std::get_if<SavedValue::List>(&item.v);
                    if (!pair || pair->size() != 2) return std::nullopt;
                    const auto* k = std::get_if<std::string>(&(*pair)[0].v);
                    if (!k) return std::nullopt;
                    m[*k] = (*pair)[1];
                }
                return restore(m);
            },
        };
    }
    // Reads a saved number (integer or float) or nullopt.
    inline std::optional<double> SavedNumber(const SavedValue& v) {
        if (const auto* i = std::get_if<std::int64_t>(&v.v)) return double(*i);
        if (const auto* d = std::get_if<double>(&v.v)) return *d;
        return std::nullopt;
    }

    class SaveableStateRegistry {
    public:
        using Values = std::map<std::string, std::vector<SavedValue>>;
        explicit SaveableStateRegistry(Values restored = {}) : m_Restored(std::move(restored)) {}

        // The first restored value under key (later ones stay for the next caller).
        std::optional<SavedValue> ConsumeRestored(const std::string& key);
        // Returns an id for Unregister.
        std::uint64_t RegisterProvider(const std::string& key, std::function<SavedValue()> provider);
        void Unregister(const std::string& key, std::uint64_t id);
        // Restored values not consumed yet are kept (for content not composed this time).
        Values PerformSave() const;

        // Expires with the registry: holders of a raw pointer check it before unregistering.
        std::weak_ptr<const char> AliveToken() const { return m_Alive; }

        static std::vector<std::uint8_t> Serialize(const Values& values);
        static Values Deserialize(std::span<const std::uint8_t> bytes);   // empty on malformed data

    private:
        Values m_Restored;
        std::map<std::string, std::vector<std::pair<std::uint64_t, std::function<SavedValue()>>>> m_Providers;
        std::uint64_t m_NextId = 1;
        std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
    };

    inline Local<SaveableStateRegistry*> LocalSaveableStateRegistry{ nullptr };

    // SaveableStateHolder (SaveableStateHolder.kt): a child registry per key for content that
    // comes and goes (lazy items, pager pages, navigation entries). Closing a key keeps its
    // values for when it is opened again; everything is saved into the parent registry too.
    class SaveableStateHolder {
    public:
        // The registry to provide for `key`'s content, restored from what Close saved.
        std::shared_ptr<SaveableStateRegistry> Open(const std::string& key);
        // Call while the content is still composed (before disposing it).
        void Close(const std::string& key);
        void RemoveState(const std::string& key) { m_Saved.erase(key); m_Live.erase(key); }
        SavedValue Save() const;
        void Restore(const SavedValue& saved);

    private:
        std::map<std::string, SaveableStateRegistry::Values> m_Saved;
        std::map<std::string, std::shared_ptr<SaveableStateRegistry>> m_Live;
    };

    // rememberSaveableStateHolder(): its values ride in the parent registry.
    SaveableStateHolder& RememberSaveableStateHolder(Key key = {});
    // SaveableStateHolder.SaveableStateProvider(key, content): `content` saves into `key`'s
    // registry; when it leaves the composition its values are kept for its return.
    void SaveableStateProvider(SaveableStateHolder& holder, const std::string& key, std::function<void()> content);

    namespace detail {
        std::string SaveableKey(std::uint64_t compositeKey);   // base-36 like Compose

        template<typename T>
        struct SaveableHolder {
            T value;
            Saver<T> saver;
            Keys inputs;
        };
        template<typename T>
        struct SaveableStateHolder {
            std::unique_ptr<State<T>> state;
            Saver<T> saver;
        };
    }

    // rememberSaveable(inputs) { init() }: restored from the registry on re-creation, re-created
    // by init() when inputs change.
    template<typename T>
    T& RememberSaveable(std::function<T()> init, Saver<T> saver = AutoSaver<T>(), Keys inputs = {}, Key key = {}) {
        auto& c = *Composition::Current();
        SaveableStateRegistry* registry = c.Read(LocalSaveableStateRegistry);
        const std::string k = detail::SaveableKey(c.CompositeKey(key));
        auto& holder = c.Remember<std::shared_ptr<detail::SaveableHolder<T>>>([&] {
            std::optional<T> restored;
            if (registry) if (auto s = registry->ConsumeRestored(k)) restored = saver.restore(*s);
            return std::make_shared<detail::SaveableHolder<T>>(
                detail::SaveableHolder<T>{ restored ? std::move(*restored) : init(), saver, inputs });
        }, {}, key);
        if (!(holder->inputs == inputs)) { holder->value = init(); holder->inputs = inputs; }
        holder->saver = std::move(saver);
        auto* h = holder.get();
        c.DisposableEffect(Keys(registry, k), [registry, k, h, keep = holder] {
            if (!registry) return std::function<void()>([] {});
            const std::uint64_t id = registry->RegisterProvider(k, [h] { return h->saver.save(h->value); });
            return std::function<void()>([registry, alive = registry->AliveToken(), k, id] { if (!alive.expired()) registry->Unregister(k, id); });
        }, key);
        return holder->value;
    }

    // rememberSaveable { mutableStateOf(initial) } with the state's value saved.
    template<typename T>
    State<T>& RememberSaveableState(T initial, Saver<T> saver = AutoSaver<T>(), Key key = {}) {
        auto& c = *Composition::Current();
        SaveableStateRegistry* registry = c.Read(LocalSaveableStateRegistry);
        const std::string k = detail::SaveableKey(c.CompositeKey(key));
        auto& holder = c.Remember<std::shared_ptr<detail::SaveableStateHolder<T>>>([&] {
            std::optional<T> restored;
            if (registry) if (auto s = registry->ConsumeRestored(k)) restored = saver.restore(*s);
            auto h = std::make_shared<detail::SaveableStateHolder<T>>();
            h->state = std::make_unique<State<T>>(restored ? std::move(*restored) : std::move(initial));
            h->saver = saver;
            return h;
        }, {}, key);
        holder->saver = std::move(saver);
        auto* h = holder.get();
        c.DisposableEffect(Keys(registry, k), [registry, k, h, keep = holder] {
            if (!registry) return std::function<void()>([] {});
            const std::uint64_t id = registry->RegisterProvider(k, [h] { return h->saver.save(h->state->Peek()); });
            return std::function<void()>([registry, alive = registry->AliveToken(), k, id] { if (!alive.expired()) registry->Unregister(k, id); });
        }, key);
        return *holder->state;
    }

}
