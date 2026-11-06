#include "kiln/runtime/Saveable.h"

#include <cstring>

namespace kiln::rt {

    std::optional<SavedValue> SaveableStateRegistry::ConsumeRestored(const std::string& key) {
        auto it = m_Restored.find(key);
        if (it == m_Restored.end() || it->second.empty()) return std::nullopt;
        SavedValue v = std::move(it->second.front());
        it->second.erase(it->second.begin());
        if (it->second.empty()) m_Restored.erase(it);
        if (v.IsNull()) return std::nullopt;
        return v;
    }

    std::uint64_t SaveableStateRegistry::RegisterProvider(const std::string& key, std::function<SavedValue()> provider) {
        const std::uint64_t id = m_NextId++;
        m_Providers[key].emplace_back(id, std::move(provider));
        return id;
    }

    void SaveableStateRegistry::Unregister(const std::string& key, std::uint64_t id) {
        auto it = m_Providers.find(key);
        if (it == m_Providers.end()) return;
        std::erase_if(it->second, [id](const auto& p) { return p.first == id; });
        if (it->second.empty()) m_Providers.erase(it);
    }

    SaveableStateRegistry::Values SaveableStateRegistry::PerformSave() const {
        Values out = m_Restored;
        for (const auto& [key, list] : m_Providers) {
            if (list.size() == 1) {
                SavedValue v = list[0].second();
                if (!v.IsNull()) out[key] = { std::move(v) };
                else out.erase(key);
            } else {
                std::vector<SavedValue> values;
                values.reserve(list.size());
                for (const auto& p : list) values.push_back(p.second());   // nulls keep the order
                out[key] = std::move(values);
            }
        }
        return out;
    }

    // --- serialisation: a small tagged format (one blob in the Activity bundle) ----------------

    namespace {
        enum Tag : std::uint8_t { Null, False, True, Int, Float, Str, List };
        constexpr std::uint32_t kMagic = 0x4B534156;   // "KSAV"

        void PutU32(std::vector<std::uint8_t>& o, std::uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(std::uint8_t(v >> (8 * i))); }
        void PutU64(std::vector<std::uint8_t>& o, std::uint64_t v) { for (int i = 0; i < 8; ++i) o.push_back(std::uint8_t(v >> (8 * i))); }
        void PutStr(std::vector<std::uint8_t>& o, const std::string& s) { PutU32(o, std::uint32_t(s.size())); o.insert(o.end(), s.begin(), s.end()); }

        void Put(std::vector<std::uint8_t>& o, const SavedValue& v) {
            std::visit([&](const auto& x) {
                using X = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<X, std::monostate>) o.push_back(Null);
                else if constexpr (std::is_same_v<X, bool>) o.push_back(x ? True : False);
                else if constexpr (std::is_same_v<X, std::int64_t>) { o.push_back(Int); PutU64(o, std::uint64_t(x)); }
                else if constexpr (std::is_same_v<X, double>) { o.push_back(Float); std::uint64_t b; std::memcpy(&b, &x, 8); PutU64(o, b); }
                else if constexpr (std::is_same_v<X, std::string>) { o.push_back(Str); PutStr(o, x); }
                else { o.push_back(List); PutU32(o, std::uint32_t(x.size())); for (const auto& e : x) Put(o, e); }
            }, v.v);
        }

        struct Reader {
            std::span<const std::uint8_t> b;
            std::size_t at = 0;
            bool ok = true;
            bool Need(std::size_t n) { if (at + n > b.size()) ok = false; return ok; }
            std::uint8_t U8() { return Need(1) ? b[at++] : 0; }
            std::uint32_t U32() { std::uint32_t v = 0; if (Need(4)) for (int i = 0; i < 4; ++i) v |= std::uint32_t(b[at++]) << (8 * i); return v; }
            std::uint64_t U64() { std::uint64_t v = 0; if (Need(8)) for (int i = 0; i < 8; ++i) v |= std::uint64_t(b[at++]) << (8 * i); return v; }
            std::string ReadStr() {
                const std::uint32_t n = U32();
                if (!Need(n)) return {};
                std::string s(reinterpret_cast<const char*>(b.data() + at), n);
                at += n;
                return s;
            }
            SavedValue Value(int depth = 0) {
                if (depth > 64) { ok = false; return {}; }
                switch (U8()) {
                    case Null: return {};
                    case False: return SavedValue(false);
                    case True: return SavedValue(true);
                    case Int: return SavedValue(std::int64_t(U64()));
                    case Float: { const std::uint64_t u = U64(); double d; std::memcpy(&d, &u, 8); return SavedValue(d); }
                    case Str: return SavedValue(ReadStr());
                    case List: {
                        const std::uint32_t n = U32();
                        SavedValue::List l;
                        for (std::uint32_t i = 0; i < n && ok; ++i) l.push_back(Value(depth + 1));
                        return SavedValue(std::move(l));
                    }
                    default: ok = false; return {};
                }
            }
        };
    }

    std::vector<std::uint8_t> SaveableStateRegistry::Serialize(const Values& values) {
        std::vector<std::uint8_t> o;
        PutU32(o, kMagic);
        PutU32(o, std::uint32_t(values.size()));
        for (const auto& [key, list] : values) {
            PutStr(o, key);
            PutU32(o, std::uint32_t(list.size()));
            for (const auto& v : list) Put(o, v);
        }
        return o;
    }

    SaveableStateRegistry::Values SaveableStateRegistry::Deserialize(std::span<const std::uint8_t> bytes) {
        Reader r{ bytes };
        if (bytes.empty() || r.U32() != kMagic) return {};
        Values out;
        const std::uint32_t n = r.U32();
        for (std::uint32_t i = 0; i < n && r.ok; ++i) {
            std::string key = r.ReadStr();
            const std::uint32_t m = r.U32();
            std::vector<SavedValue> list;
            for (std::uint32_t j = 0; j < m && r.ok; ++j) list.push_back(r.Value());
            out.emplace(std::move(key), std::move(list));
        }
        return r.ok ? out : Values{};
    }

    namespace detail {
        std::string SaveableKey(std::uint64_t k) {
            const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
            std::string s;
            do { s.insert(s.begin(), digits[k % 36]); k /= 36; } while (k);
            return s;
        }
    }

}
