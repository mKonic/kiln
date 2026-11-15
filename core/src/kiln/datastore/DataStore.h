#pragma once

// Typed DataStore (datastore-core: DataStore, DataStoreFactory, Serializer, DataMigration,
// CorruptionHandler, DataStoreImpl): one value of the app's own type in a file, read as a flow and
// changed with UpdateData, which runs transforms one at a time and writes the file atomically
// (temporary file, fsync, rename) on the IO pool. For key-value settings, Preferences.h.
//
//   struct Settings { std::string theme = "light"; int fontSize = 14; bool operator==(const Settings&) const = default; };
//   datastore::Serializer<Settings> serializer{
//       .defaultValue = {},
//       .readFrom = [](std::string_view bytes) { return Parse(bytes); },   // throw CorruptionException when it is not one
//       .writeTo = [](const Settings& s) { return Write(s); },
//   };
//   auto store = datastore::DataStoreFactory::Create(serializer, datastore::DataStoreFile("settings.pb"));
//   co_await store->UpdateData([](Settings s) { s.fontSize = 16; return s; });
//
// One store per file at a time (a second Create for a file in use throws, as upstream).

#include "kiln/runtime/Channel.h"
#include "kiln/runtime/Flow.h"
#include "kiln/runtime/Task.h"
#include "kiln/runtime/TaskDispatchers.h"
#include <kiln/Function.h>

#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kiln::datastore {

    // The bytes on disk are not a value (Serializer::readFrom throws it).
    class CorruptionException : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    template<typename T>
    struct Serializer {
        T defaultValue{};   // what a store without a file reads
        kiln::Function<T(std::string_view bytes)> readFrom;
        kiln::Function<std::string(const T& value)> writeTo;
    };

    // Run once, as the store first reads: migrate when shouldMigrate, then cleanUp once the
    // migrated value is written (DataMigrationInitializer).
    template<typename T>
    struct DataMigration {
        kiln::Function<bool(const T& current)> shouldMigrate;
        kiln::Function<T(T current)> migrate;
        kiln::Function<void()> cleanUp;
    };

    // What a corrupt file is replaced with (written back); without one, reads throw
    // CorruptionException.
    template<typename T>
    using CorruptionHandler = kiln::Function<T(const CorruptionException&)>;
    // ReplaceFileCorruptionHandler(produceNewData).
    template<typename T>
    CorruptionHandler<T> ReplaceFileCorruptionHandler(kiln::Function<T(const CorruptionException&)> produceNewData) { return produceNewData; }

    // dataStoreFile(name): <filesDir>/datastore/<name>.
    std::string DataStoreFile(const std::string& fileName);

    namespace detail {
        // nullopt when there is no file; other failures throw (FileStorage's readData).
        std::optional<std::string> ReadFile(const std::string& path);
        // A temporary file next to `path`, synced and renamed over it.
        void WriteFileAtomically(const std::string& path, const std::string& bytes);
        // One live store per file: false when another holds it.
        bool ClaimFile(const std::string& path, std::weak_ptr<const void> owner);

        // A FIFO lock for coroutines: the first read and the updates take turns.
        class TurnLock {
        public:
            rt::Task<> Lock();
            void Unlock();
            struct Guard {
                std::shared_ptr<TurnLock> lock;
                ~Guard() { lock->Unlock(); }
            };
        private:
            bool m_Locked = false;
            std::deque<std::weak_ptr<rt::Channel<bool>>> m_Waiters;
        };
    }

    template<typename T>
    class DataStore : public std::enable_shared_from_this<DataStore<T>> {
    public:
        DataStore(Serializer<T> serializer, std::string path, std::optional<CorruptionHandler<T>> corruptionHandler,
                  std::vector<DataMigration<T>> migrations)
            : m_Serializer(std::move(serializer)), m_Path(std::move(path)), m_Corruption(std::move(corruptionHandler)),
              m_Migrations(std::move(migrations)), m_State(m_Serializer.defaultValue), m_Turns(std::make_shared<detail::TurnLock>()) {}

        // data: the value, read on first use (after the migrations), then each change. A file
        // that cannot be read throws from the collection; the next one tries again.
        rt::Flow<T> Data() {
            return rt::Flow<T>::Create([self = this->shared_from_this()](rt::FlowCollector<T> emit) -> rt::Task<> {
                co_await self->EnsureLoaded();
                co_await self->m_State.Collect(emit);
            });
        }

        // updateData: transform of the latest value, written when it differs; the new value.
        // Transforms run one at a time in the order made; a failed write throws and keeps the
        // old value.
        rt::Task<T> UpdateData(kiln::Function<T(T current)> transform) {
            auto self = this->shared_from_this();
            auto turns = self->m_Turns;
            co_await turns->Lock();
            detail::TurnLock::Guard unlock{ turns };
            co_await self->LoadLocked();
            T next = transform(self->m_State.Value());
            if (next == self->m_State.Value()) co_return next;
            co_await self->Write(next);
            self->m_State.SetValue(next);
            co_return next;
        }

        // The value now (the default until the file was read).
        const T& Current() const { return m_State.Value(); }
        const std::string& Path() const { return m_Path; }

    private:
        rt::Task<> Write(const T& value) {
            const std::string bytes = m_Serializer.writeTo(value);
            const std::string path = m_Path;
            co_await rt::WithContext(rt::TaskDispatchers::Platform().IO(), [path, bytes] { detail::WriteFileAtomically(path, bytes); });
        }

        rt::Task<> EnsureLoaded() {
            if (m_Loaded) co_return;
            auto self = this->shared_from_this();
            auto turns = self->m_Turns;
            co_await turns->Lock();
            detail::TurnLock::Guard unlock{ turns };
            co_await self->LoadLocked();
        }

        // With the turn held: the file, then the migrations (once, before anything reads).
        rt::Task<> LoadLocked() {
            if (m_Loaded) co_return;
            auto self = this->shared_from_this();
            const std::string path = self->m_Path;
            const std::optional<std::string> bytes = co_await rt::WithContext(rt::TaskDispatchers::Platform().IO(), [path] { return detail::ReadFile(path); });
            T value = self->m_Serializer.defaultValue;
            if (bytes) {
                std::optional<CorruptionException> corrupt;
                try {
                    value = self->m_Serializer.readFrom(*bytes);
                } catch (const CorruptionException& e) {
                    if (!self->m_Corruption) throw;
                    corrupt = e;
                }
                if (corrupt) {
                    // handleCorruption: the replacement is written before it is served.
                    value = (*self->m_Corruption)(*corrupt);
                    co_await self->Write(value);
                }
            }
            // DataMigrationInitializer: one update over every migration that applies, then the
            // clean-ups of those that ran.
            std::vector<kiln::Function<void()>> cleanUps;
            T migrated = value;
            for (auto& m : self->m_Migrations) {
                if (m.shouldMigrate && m.shouldMigrate(migrated)) {
                    if (m.cleanUp) cleanUps.push_back(m.cleanUp);
                    migrated = m.migrate(std::move(migrated));
                }
            }
            if (!(migrated == value)) co_await self->Write(migrated);
            self->m_Migrations.clear();
            self->m_State.SetValue(std::move(migrated));
            self->m_Loaded = true;
            std::exception_ptr first;
            for (auto& c : cleanUps) {
                try { c(); } catch (...) { if (!first) first = std::current_exception(); }
            }
            if (first) std::rethrow_exception(first);
        }

        Serializer<T> m_Serializer;
        std::string m_Path;
        std::optional<CorruptionHandler<T>> m_Corruption;
        std::vector<DataMigration<T>> m_Migrations;
        rt::MutableStateFlow<T> m_State;
        bool m_Loaded = false;
        std::shared_ptr<detail::TurnLock> m_Turns;
    };

    struct DataStoreFactory {
        // DataStoreFactory.create(serializer, corruptionHandler, migrations, produceFile).
        // Throws std::logic_error while another store has the file.
        template<typename T>
        static std::shared_ptr<DataStore<T>> Create(Serializer<T> serializer, std::string path,
                                                    std::type_identity_t<std::optional<CorruptionHandler<T>>> corruptionHandler = std::nullopt,
                                                    std::type_identity_t<std::vector<DataMigration<T>>> migrations = {}) {
            auto store = std::make_shared<DataStore<T>>(std::move(serializer), path, std::move(corruptionHandler), std::move(migrations));
            if (!detail::ClaimFile(path, store))
                throw std::logic_error("There are multiple DataStores active for the same file: " + path +
                                       ". You should either maintain your DataStore as a singleton or confirm that there is no two DataStore's active on the same file.");
            return store;
        }
    };

}
