#pragma once

// DataStore Preferences (datastore-preferences-core: Preferences.kt, PreferencesKeys.kt,
// PreferenceDataStoreFactory; datastore-core's SingleProcessDataStore): typed key-value data in a
// file under the app's files directory, read as a state flow and changed with Edit, which applies
// edits in order and writes the file atomically (temporary file, fsync, rename) on the IO pool.
// One store per file in the process (as DataStore requires); Open returns the same one.
//
//   inline const auto kTheme = datastore::StringKey("theme");
//   auto store = datastore::PreferencesDataStore::Open("settings");
//   auto& prefs = datastore::CollectAsState(store);   // in content
//   std::string theme = prefs.Get().Get(kTheme).value_or("light");
//   store->Update([](datastore::MutablePreferences& p) { p.Set(kTheme, std::string("dark")); });

#include "kiln/datastore/DataStore.h"
#include "kiln/runtime/Flow.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace kiln::datastore {

    using PreferenceValue = std::variant<bool, std::int32_t, std::int64_t, float, double, std::string, std::set<std::string>, std::vector<std::uint8_t>>;

    template<typename T>
    struct PreferenceKey {
        std::string name;
        bool operator==(const PreferenceKey&) const = default;
    };
    // booleanPreferencesKey, intPreferencesKey, ...
    inline PreferenceKey<bool> BoolKey(std::string name) { return { std::move(name) }; }
    inline PreferenceKey<std::int32_t> IntKey(std::string name) { return { std::move(name) }; }
    inline PreferenceKey<std::int64_t> LongKey(std::string name) { return { std::move(name) }; }
    inline PreferenceKey<float> FloatKey(std::string name) { return { std::move(name) }; }
    inline PreferenceKey<double> DoubleKey(std::string name) { return { std::move(name) }; }
    inline PreferenceKey<std::string> StringKey(std::string name) { return { std::move(name) }; }
    inline PreferenceKey<std::set<std::string>> StringSetKey(std::string name) { return { std::move(name) }; }
    inline PreferenceKey<std::vector<std::uint8_t>> ByteArrayKey(std::string name) { return { std::move(name) }; }

    class Preferences {
    public:
        template<typename T> std::optional<T> Get(const PreferenceKey<T>& key) const {
            auto it = m_Values.find(key.name);
            if (it == m_Values.end()) return std::nullopt;
            if (const T* v = std::get_if<T>(&it->second)) return *v;
            return std::nullopt;   // stored with another type (ClassCastException in Kotlin)
        }
        template<typename T> bool Contains(const PreferenceKey<T>& key) const { return m_Values.contains(key.name); }
        const std::map<std::string, PreferenceValue>& AsMap() const { return m_Values; }
        std::size_t Size() const { return m_Values.size(); }
        bool operator==(const Preferences&) const = default;

    protected:
        std::map<std::string, PreferenceValue> m_Values;
        friend class PreferencesDataStore;
        friend std::optional<Preferences> ParsePreferences(std::string_view bytes);
    };

    class MutablePreferences : public Preferences {
    public:
        MutablePreferences() = default;
        explicit MutablePreferences(const Preferences& p) : Preferences(p) {}
        template<typename T> void Set(const PreferenceKey<T>& key, T value) { m_Values[key.name] = PreferenceValue(std::move(value)); }
        template<typename T> void Remove(const PreferenceKey<T>& key) { m_Values.erase(key.name); }
        // putAll / plusAssign(prefs): every entry of other, replacing those with the same name.
        void PutAll(const Preferences& other) {
            for (const auto& [name, value] : other.AsMap()) m_Values[name] = value;
        }
        void Clear() { m_Values.clear(); }
    };

    // The file format: "KPREF1", then per entry a type byte, the name and the value.
    std::string SerializePreferences(const Preferences& p);
    std::optional<Preferences> ParsePreferences(std::string_view bytes);

    class PreferencesDataStore : public std::enable_shared_from_this<PreferencesDataStore> {
    public:
        // preferencesDataStore(name): <filesDir>/datastore/<name>.preferences_pb's kiln
        // counterpart (<name>.preferences). The same store for the same name.
        static std::shared_ptr<PreferencesDataStore> Open(const std::string& name);
        // A store at `path` (tests, or a file elsewhere).
        static std::shared_ptr<PreferencesDataStore> OpenFile(const std::string& path);

        // data: the current preferences, read from the file on first use, then each change. A
        // missing file is empty preferences; a file that cannot be read throws (from the flow,
        // and from Edit), and the next read tries again.
        rt::Flow<Preferences> Data();
        // The preferences now (empty until the file was read).
        const Preferences& Current() const { return m_State.Value(); }
        // edit { }: applied to the latest preferences, then written; the flow emits once the
        // file is written. Edits run one at a time in the order they were made. A failed write
        // throws from the task and leaves the data unchanged.
        rt::Task<> Edit(kiln::Function<void(MutablePreferences&)> transform);
        // Edit without a scope to launch it in (a click handler): the store keeps the edit
        // running. A failed write is logged, not thrown.
        void Update(kiln::Function<void(MutablePreferences&)> transform);
        const std::string& Path() const { return m_Path; }

        explicit PreferencesDataStore(std::string path);

    private:
        rt::Task<> EnsureLoaded();
        rt::Task<> LoadLocked();
        std::string m_Path;
        rt::MutableStateFlow<Preferences> m_State{ Preferences{} };
        bool m_Loaded = false;
        std::shared_ptr<detail::TurnLock> m_Turns;   // the FIFO lock the first read and edits take
        std::vector<rt::Job> m_Updates;
    };

    // The store's preferences as state, collected while the caller is composed: content that
    // reads it recomposes after each edit.
    inline rt::State<Preferences>& CollectAsState(const std::shared_ptr<PreferencesDataStore>& store, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& state = c.Remember<std::shared_ptr<rt::State<Preferences>>>([&] { return std::make_shared<rt::State<Preferences>>(store->Current()); }, rt::Keys(reinterpret_cast<std::uintptr_t>(store.get())), key);
        std::shared_ptr<rt::State<Preferences>> out = state;
        c.LaunchedEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(store.get())), [store, out] {
            return store->Data().Collect(kiln::Function<void(const Preferences&)>([out](const Preferences& p) { out->Set(p); }));
        }, key);
        return *state;
    }

}
