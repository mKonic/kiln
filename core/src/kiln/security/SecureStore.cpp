#include "kiln/security/SecureStore.h"

#include "kiln/Log.h"
#include "kiln/ui/AppDirectories.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace kiln::security {

    namespace {
        struct Registry {
            std::mutex mutex;
            CipherHost host;
            std::map<std::string, std::weak_ptr<SecureStore>> stores;
        };
        Registry& R() {
            static Registry* r = new Registry;
            return *r;
        }
        CipherHost Host() {
            std::lock_guard lock(R().mutex);
            return R().host;
        }

        // count, then (key length, key, value length, value) each, lengths as 4 little-endian bytes.
        void PutU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
            for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
        }
        std::vector<std::uint8_t> Encode(const std::map<std::string, std::string>& values) {
            std::vector<std::uint8_t> out;
            PutU32(out, std::uint32_t(values.size()));
            for (const auto& [k, v] : values) {
                PutU32(out, std::uint32_t(k.size()));
                out.insert(out.end(), k.begin(), k.end());
                PutU32(out, std::uint32_t(v.size()));
                out.insert(out.end(), v.begin(), v.end());
            }
            return out;
        }
        std::optional<std::map<std::string, std::string>> Decode(const std::vector<std::uint8_t>& in) {
            std::size_t at = 0;
            auto u32 = [&](std::uint32_t& v) {
                if (in.size() - at < 4) return false;
                v = 0;
                for (int i = 0; i < 4; ++i) v |= std::uint32_t(in[at + i]) << (8 * i);
                at += 4;
                return true;
            };
            auto str = [&](std::string& s) {
                std::uint32_t n;
                if (!u32(n) || in.size() - at < n) return false;
                s.assign(in.begin() + std::ptrdiff_t(at), in.begin() + std::ptrdiff_t(at + n));
                at += n;
                return true;
            };
            std::map<std::string, std::string> out;
            std::uint32_t count;
            if (!u32(count)) return std::nullopt;
            for (std::uint32_t i = 0; i < count; ++i) {
                std::string k, v;
                if (!str(k) || !str(v)) return std::nullopt;
                out[std::move(k)] = std::move(v);
            }
            if (at != in.size()) return std::nullopt;
            return out;
        }
    }

    void SetCipherHost(CipherHost host) {
        std::lock_guard lock(R().mutex);
        R().host = std::move(host);
    }

    std::shared_ptr<SecureStore> SecureStore::Open(const std::string& name) {
        {
            std::lock_guard lock(R().mutex);
            if (auto s = R().stores[name].lock()) return s;
        }
        auto made = std::make_shared<SecureStore>(name);   // reads the host: outside the lock
        std::lock_guard lock(R().mutex);
        auto& slot = R().stores[name];
        if (auto s = slot.lock()) return s;                 // another thread opened it meanwhile
        slot = made;
        return made;
    }

    SecureStore::SecureStore(std::string name) : m_Name(std::move(name)) {
        m_Persistent = bool(Host().encrypt);
        if (!m_Persistent) {
            log::Warn("kiln", "SecureStore {}: no platform key store; values are kept in memory only", m_Name);
            return;
        }
        m_Path = ui::NoBackupFilesDir() + "/kiln_secure/" + m_Name + ".bin";
        Load();
    }

    void SecureStore::Load() {
        std::ifstream f(m_Path, std::ios::binary);
        if (!f) return;
        std::vector<std::uint8_t> sealed((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto plain = Host().decrypt(Alias(), sealed, m_Name);
        auto values = plain ? Decode(*plain) : std::nullopt;
        if (!values) {
            log::Error("kiln", "SecureStore {}: cannot be decrypted (its key is gone or the file changed); starting empty", m_Name);
            return;
        }
        m_Values = std::move(*values);
    }

    void SecureStore::Save() {
        if (!m_Persistent) return;
        const std::vector<std::uint8_t> sealed = Host().encrypt(Alias(), Encode(m_Values), m_Name);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(m_Path).parent_path(), ec);
        // Written whole, then moved over the old file.
        const std::string tmp = m_Path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(sealed.data()), std::streamsize(sealed.size()));
            if (!f) {
                log::Error("kiln", "SecureStore {}: cannot write {}", m_Name, tmp);
                return;
            }
        }
        std::filesystem::rename(tmp, m_Path, ec);
        if (ec) log::Error("kiln", "SecureStore {}: cannot replace {}: {}", m_Name, m_Path, ec.message());
    }

    std::string SecureStore::Alias() const { return "kiln_secure_" + m_Name; }

    std::optional<std::string> SecureStore::Get(const std::string& key) const {
        std::lock_guard lock(m_Mutex);
        auto it = m_Values.find(key);
        if (it == m_Values.end()) return std::nullopt;
        return it->second;
    }
    void SecureStore::Set(const std::string& key, const std::string& value) {
        std::lock_guard lock(m_Mutex);
        m_Values[key] = value;
        Save();
    }
    void SecureStore::Remove(const std::string& key) {
        std::lock_guard lock(m_Mutex);
        if (m_Values.erase(key)) Save();
    }
    void SecureStore::Clear() {
        std::lock_guard lock(m_Mutex);
        m_Values.clear();
        Save();
    }
    std::vector<std::string> SecureStore::Keys() const {
        std::lock_guard lock(m_Mutex);
        std::vector<std::string> out;
        for (const auto& [k, v] : m_Values) out.push_back(k);
        return out;
    }

}
