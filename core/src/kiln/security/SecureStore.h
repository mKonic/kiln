#pragma once

// Secrets at rest (what EncryptedSharedPreferences did; androidx.security's guidance now is a
// key in the Android Keystore encrypting the data): string values encrypted with AES-256-GCM
// under a key the Keystore generates and never lets out of its secure hardware.
//
//   auto store = security::SecureStore::Open("session");
//   store->Set("token", token);
//   std::optional<std::string> t = store->Get("token");
//
// The whole store (keys and values) is one encrypted file in NoBackupFilesDir: Keystore keys do
// not move with a backup, so a restored copy could never be read. If the file cannot be
// decrypted (the key is gone: the app's data was cleared or the lock screen reset), the store
// starts empty. Without a platform key store (the desktop) values live in memory for the
// process and are never written in plain text.

#include <kiln/Function.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace kiln::security {

    class SecureStore {
    public:
        // One store per name in the process.
        static std::shared_ptr<SecureStore> Open(const std::string& name);

        std::optional<std::string> Get(const std::string& key) const;
        void Set(const std::string& key, const std::string& value);
        void Remove(const std::string& key);
        void Clear();
        std::vector<std::string> Keys() const;
        // Whether values are written to storage (a platform key store is there).
        bool Persistent() const { return m_Persistent; }

        explicit SecureStore(std::string name);

    private:
        void Load();
        void Save();
        std::string Alias() const;
        std::string m_Name, m_Path;
        bool m_Persistent = false;
        mutable std::mutex m_Mutex;
        std::map<std::string, std::string> m_Values;
    };

    // The platform's key store (Android installs the Keystore's): AES-256-GCM under the key named
    // `alias`, made on first use; the associated data is authenticated, not encrypted. Encrypt
    // returns the nonce and the ciphertext with its tag; Decrypt returns nullopt when the data was
    // changed or the key is gone.
    struct CipherHost {
        kiln::Function<std::vector<std::uint8_t>(const std::string& alias, const std::vector<std::uint8_t>& plain,
                                                 const std::string& associatedData)> encrypt;
        kiln::Function<std::optional<std::vector<std::uint8_t>>(const std::string& alias, const std::vector<std::uint8_t>& sealed,
                                                                const std::string& associatedData)> decrypt;
    };
    void SetCipherHost(CipherHost host);

}
