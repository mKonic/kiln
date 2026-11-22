#include "kiln/security/KeyStore.h"

namespace kiln::security {

    namespace {
        KeyStoreHost& Host() {
            static auto* h = new KeyStoreHost;
            return *h;
        }
    }

    bool GenerateKey(const std::string& alias, const KeyGenSpec& spec) { return Host().generate && Host().generate(alias, spec); }
    bool DeleteKey(const std::string& alias) { return Host().remove && Host().remove(alias); }
    bool ContainsKey(const std::string& alias) { return Host().contains && Host().contains(alias); }
    std::optional<std::vector<std::uint8_t>> GetPublicKey(const std::string& alias) {
        return Host().publicKey ? Host().publicKey(alias) : std::nullopt;
    }
    void SetKeyStoreHost(KeyStoreHost host) { Host() = std::move(host); }

}
