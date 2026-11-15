#include "kiln/datastore/Preferences.h"

#include "kiln/runtime/TaskDispatchers.h"
#include "kiln/ui/AppDirectories.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace kiln::datastore {

    namespace {
        enum : std::uint8_t { kBool = 1, kInt = 2, kLong = 3, kFloat = 4, kDouble = 5, kString = 6, kStringSet = 7, kBytes = 8 };
        constexpr std::string_view kMagic = "KPREF1";

        void PutU32(std::string& out, std::uint32_t v) { for (int i = 0; i < 4; ++i) out += char((v >> (8 * i)) & 0xff); }
        void PutBytes(std::string& out, std::string_view s) { PutU32(out, std::uint32_t(s.size())); out += s; }
        template<typename T> void PutRaw(std::string& out, T v) { char b[sizeof(T)]; std::memcpy(b, &v, sizeof(T)); out.append(b, sizeof(T)); }

        struct Reader {
            std::string_view s;
            std::size_t at = 0;
            bool ok = true;
            std::uint32_t U32() {
                if (at + 4 > s.size()) { ok = false; return 0; }
                std::uint32_t v = 0;
                for (int i = 0; i < 4; ++i) v |= std::uint32_t(static_cast<unsigned char>(s[at + std::size_t(i)])) << (8 * i);
                at += 4;
                return v;
            }
            std::string Bytes() {
                const std::uint32_t n = U32();
                if (!ok || at + n > s.size()) { ok = false; return {}; }
                std::string out(s.substr(at, n));
                at += n;
                return out;
            }
            std::uint8_t Byte() {
                if (at >= s.size()) { ok = false; return 0; }
                return static_cast<std::uint8_t>(s[at++]);
            }
            template<typename T> T Raw() {
                if (at + sizeof(T) > s.size()) { ok = false; return T{}; }
                T v;
                std::memcpy(&v, s.data() + at, sizeof(T));
                at += sizeof(T);
                return v;
            }
        };

        std::mutex g_StoresMutex;
        std::map<std::string, std::weak_ptr<PreferencesDataStore>>& Stores() {
            static auto* m = new std::map<std::string, std::weak_ptr<PreferencesDataStore>>;
            return *m;
        }
    }

    std::string SerializePreferences(const Preferences& p) {
        std::string out(kMagic);
        PutU32(out, std::uint32_t(p.AsMap().size()));
        for (const auto& [name, v] : p.AsMap()) {
            std::visit([&](const auto& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, bool>) { out += char(kBool); PutBytes(out, name); out += char(x ? 1 : 0); }
                else if constexpr (std::is_same_v<T, std::int32_t>) { out += char(kInt); PutBytes(out, name); PutRaw(out, x); }
                else if constexpr (std::is_same_v<T, std::int64_t>) { out += char(kLong); PutBytes(out, name); PutRaw(out, x); }
                else if constexpr (std::is_same_v<T, float>) { out += char(kFloat); PutBytes(out, name); PutRaw(out, x); }
                else if constexpr (std::is_same_v<T, double>) { out += char(kDouble); PutBytes(out, name); PutRaw(out, x); }
                else if constexpr (std::is_same_v<T, std::string>) { out += char(kString); PutBytes(out, name); PutBytes(out, x); }
                else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
                    out += char(kBytes);
                    PutBytes(out, name);
                    PutBytes(out, std::string_view(reinterpret_cast<const char*>(x.data()), x.size()));
                } else {
                    out += char(kStringSet);
                    PutBytes(out, name);
                    PutU32(out, std::uint32_t(x.size()));
                    for (const auto& s : x) PutBytes(out, s);
                }
            }, v);
        }
        return out;
    }

    std::optional<Preferences> ParsePreferences(std::string_view bytes) {
        if (!bytes.starts_with(kMagic)) return std::nullopt;
        Reader r{ bytes, kMagic.size() };
        Preferences p;
        const std::uint32_t count = r.U32();
        for (std::uint32_t i = 0; i < count && r.ok; ++i) {
            const std::uint8_t type = r.Byte();
            std::string name = r.Bytes();
            switch (type) {
                case kBool: p.m_Values[name] = r.Byte() != 0; break;
                case kInt: p.m_Values[name] = r.Raw<std::int32_t>(); break;
                case kLong: p.m_Values[name] = r.Raw<std::int64_t>(); break;
                case kFloat: p.m_Values[name] = r.Raw<float>(); break;
                case kDouble: p.m_Values[name] = r.Raw<double>(); break;
                case kString: p.m_Values[name] = r.Bytes(); break;
                case kStringSet: {
                    std::set<std::string> set;
                    const std::uint32_t n = r.U32();
                    for (std::uint32_t k = 0; k < n && r.ok; ++k) set.insert(r.Bytes());
                    p.m_Values[name] = std::move(set);
                    break;
                }
                case kBytes: {
                    const std::string b = r.Bytes();
                    p.m_Values[name] = std::vector<std::uint8_t>(b.begin(), b.end());
                    break;
                }
                default: return std::nullopt;
            }
        }
        if (!r.ok) return std::nullopt;
        return p;
    }

    PreferencesDataStore::PreferencesDataStore(std::string path) : m_Path(std::move(path)), m_Turns(std::make_shared<detail::TurnLock>()) {}

    std::shared_ptr<PreferencesDataStore> PreferencesDataStore::Open(const std::string& name) {
        return OpenFile(DataStoreFile(name + ".preferences"));
    }

    std::shared_ptr<PreferencesDataStore> PreferencesDataStore::OpenFile(const std::string& path) {
        std::lock_guard lock(g_StoresMutex);
        auto& slot = Stores()[path];
        if (auto s = slot.lock()) return s;
        auto s = std::make_shared<PreferencesDataStore>(path);
        slot = s;
        return s;
    }

    // With the turn held: reads the file the first time.
    rt::Task<> PreferencesDataStore::LoadLocked() {
        if (m_Loaded) co_return;
        auto self = shared_from_this();
        const std::string path = self->m_Path;
        const std::optional<std::string> bytes = co_await rt::WithContext(rt::TaskDispatchers::Platform().IO(), [path] { return detail::ReadFile(path); });
        // A missing file is empty preferences; an unreadable one too (corruption: start over,
        // as ReplaceFileCorruptionHandler with emptyPreferences does).
        Preferences p;
        if (bytes) if (auto parsed = ParsePreferences(*bytes)) p = std::move(*parsed);
        self->m_State.SetValue(std::move(p));
        self->m_Loaded = true;
    }

    rt::Task<> PreferencesDataStore::EnsureLoaded() {
        if (m_Loaded) co_return;
        auto self = shared_from_this();
        auto mutex = self->m_Turns;
        co_await mutex->Lock();
        detail::TurnLock::Guard unlock{ mutex };
        co_await self->LoadLocked();
    }

    rt::Flow<Preferences> PreferencesDataStore::Data() {
        return rt::Flow<Preferences>::Create([self = shared_from_this()](rt::FlowCollector<Preferences> emit) -> rt::Task<> {
            co_await self->EnsureLoaded();
            co_await self->m_State.Collect(emit);
        });
    }

    rt::Task<> PreferencesDataStore::Edit(kiln::Function<void(MutablePreferences&)> transform) {
        // One turn for the first read and the edit, so edits keep the order they were made in.
        auto self = shared_from_this();
        auto mutex = self->m_Turns;
        co_await mutex->Lock();
        detail::TurnLock::Guard unlock{ mutex };
        co_await self->LoadLocked();
        MutablePreferences next(self->m_State.Value());
        transform(next);
        if (Preferences(next) == self->m_State.Value()) co_return;   // unchanged: no write
        const std::string bytes = SerializePreferences(next);
        const std::string path = self->m_Path;
        co_await rt::WithContext(rt::TaskDispatchers::Platform().IO(), [path, bytes] { detail::WriteFileAtomically(path, bytes); });
        self->m_State.SetValue(Preferences(next));
    }

    void PreferencesDataStore::Update(kiln::Function<void(MutablePreferences&)> transform) {
        std::erase_if(m_Updates, [](const rt::Job& j) { return j.Done(); });
        m_Updates.push_back(rt::Launch(rt::Dispatcher::Main(), [](std::shared_ptr<PreferencesDataStore> self, kiln::Function<void(MutablePreferences&)> t) -> rt::Task<> {
            try {
                co_await self->Edit(std::move(t));
            } catch (const std::exception& e) {
                std::fprintf(stderr, "kiln: preferences %s: %s\n", self->m_Path.c_str(), e.what());
            }
        }(shared_from_this(), std::move(transform))));
    }

}
