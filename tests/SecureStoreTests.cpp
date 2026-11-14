// Secrets at rest: one encrypted file per store, unreadable without the key store's key.
#include "Check.h"

#include "kiln/security/SecureStore.h"
#include "kiln/ui/AppDirectories.h"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace kiln;
using namespace kiln::security;

namespace {
    // A stand-in key store: XOR with the alias, a checksum over the data and the associated data
    // (the test checks what the store does with a cipher, not the cipher).
    struct FakeKeystore {
        int encrypts = 0;
        bool keyGone = false;
        static std::uint8_t Sum(const std::vector<std::uint8_t>& d, const std::string& aad) {
            std::uint8_t s = 0;
            for (auto b : d) s = std::uint8_t(s * 31 + b);
            for (char c : aad) s = std::uint8_t(s * 31 + std::uint8_t(c));
            return s;
        }
        FakeKeystore() {
            SetCipherHost({
                .encrypt = [this](const std::string& alias, const std::vector<std::uint8_t>& plain, const std::string& aad) {
                    ++encrypts;
                    std::vector<std::uint8_t> out(plain);
                    for (std::size_t i = 0; i < out.size(); ++i) out[i] ^= std::uint8_t(alias[i % alias.size()] | 0x80);
                    out.push_back(Sum(plain, aad));
                    return out;
                },
                .decrypt = [this](const std::string& alias, const std::vector<std::uint8_t>& sealed, const std::string& aad)
                    -> std::optional<std::vector<std::uint8_t>> {
                    if (keyGone || sealed.empty()) return std::nullopt;
                    std::vector<std::uint8_t> out(sealed.begin(), sealed.end() - 1);
                    for (std::size_t i = 0; i < out.size(); ++i) out[i] ^= std::uint8_t(alias[i % alias.size()] | 0x80);
                    if (Sum(out, aad) != sealed.back()) return std::nullopt;
                    return out;
                },
            });
        }
        ~FakeKeystore() { SetCipherHost({}); }
    };
    std::string Read(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
}

TEST(SecureStoreKeepsValuesEncryptedOnDisk) {
    const std::string dir = test::MakeTempDir("kiln-secure-");
    ui::SetAppDirectories(dir + "/f", dir + "/c", dir + "/n");
    FakeKeystore keystore;
    const std::string file = dir + "/n/kiln_secure/session.bin";
    {
        auto s = SecureStore::Open("session");
        CHECK(s->Persistent() && s->Keys().empty() && !s->Get("token"));
        CHECK(SecureStore::Open("session") == s);
        s->Set("token", "secret-123");
        s->Set("user", "ada");
        CHECK(s->Get("token") == std::optional<std::string>("secret-123"));
        // Neither keys nor values are on disk in the clear.
        const std::string bytes = Read(file);
        CHECK(!bytes.empty() && bytes.find("secret-123") == std::string::npos && bytes.find("token") == std::string::npos);
        s->Remove("user");
        CHECK(s->Keys() == std::vector<std::string>{ "token" });
    }
    // Opened again (a new process): read back.
    {
        auto s = SecureStore::Open("session");
        CHECK(s->Get("token") == std::optional<std::string>("secret-123") && !s->Get("user"));
    }
    // Another store's name is bound to its file: renamed, it does not open.
    std::filesystem::copy_file(file, dir + "/n/kiln_secure/other.bin");
    CHECK(SecureStore::Open("other")->Keys().empty());
    // A changed file, or a key that is gone: the store starts empty.
    {
        std::string bytes = Read(file);
        bytes[2] = char(bytes[2] ^ 1);
        std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
        CHECK(SecureStore::Open("session")->Keys().empty());
    }
    keystore.keyGone = true;
    auto s = SecureStore::Open("gone");
    s->Set("a", "b");
    s.reset();
    CHECK(SecureStore::Open("gone")->Keys().empty());
    ui::SetAppDirectories({}, {}, {});
    std::filesystem::remove_all(dir);
}

TEST(SecureStoreWithoutAKeyStoreStaysInMemory) {
    const std::string dir = test::MakeTempDir("kiln-secure-");
    ui::SetAppDirectories(dir + "/f", dir + "/c", dir + "/n");
    {
        auto s = SecureStore::Open("desk");
        CHECK(!s->Persistent());
        s->Set("token", "x");
        CHECK(s->Get("token") == std::optional<std::string>("x"));
    }
    CHECK(!std::filesystem::exists(dir + "/n/kiln_secure"));
    ui::SetAppDirectories({}, {}, {});
    std::filesystem::remove_all(dir);
}
