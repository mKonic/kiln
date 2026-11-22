#pragma once

// Keys in the Android Keystore (KeyGenParameterSpec, KeyStore): made on the device, used there
// and never exported. A key with userAuthenticationRequired unlocks only through the biometric
// prompt (ui::CryptoObject) or, with a timeout, for a while after the user authenticates.
//
//   security::GenerateKey("notes", { .userAuthenticationRequired = true });
//   auto r = co_await ui::Authenticate({ .title = "Unlock", .allowedAuthenticators = ui::kBiometricStrong },
//                                      ui::CryptoObject::Encrypt("notes", plain));
//   if (r.succeeded) Save(r.crypto->outputIv, r.crypto->output);

#include <kiln/Function.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kiln::security {

    // KeyProperties' purposes and user authentication types.
    inline constexpr int PURPOSE_ENCRYPT = 1, PURPOSE_DECRYPT = 2, PURPOSE_SIGN = 4, PURPOSE_VERIFY = 8, PURPOSE_AGREE_KEY = 64;
    inline constexpr int AUTH_DEVICE_CREDENTIAL = 1, AUTH_BIOMETRIC_STRONG = 2;

    struct KeyGenSpec {
        enum class Algorithm { AES, EC, RSA, HmacSHA256 };
        Algorithm algorithm = Algorithm::AES;
        int keySize = 0;   // 0: the algorithm's default (AES 256, EC 256, RSA 2048)
        int purposes = PURPOSE_ENCRYPT | PURPOSE_DECRYPT;
        std::vector<std::string> blockModes{ "GCM" };            // KeyProperties.BLOCK_MODE_*
        std::vector<std::string> encryptionPaddings{ "NoPadding" };
        std::vector<std::string> digests;                        // "SHA-256": signing keys need one
        std::vector<std::string> signaturePaddings;              // RSA: "PKCS1", "PSS"
        bool userAuthenticationRequired = false;
        // setUserAuthenticationParameters: 0 seconds: every use needs the prompt (a CryptoObject);
        // more: usable for that long after any authentication of the given types.
        int userAuthenticationTimeoutSeconds = 0;
        int userAuthenticationTypes = AUTH_BIOMETRIC_STRONG;
        bool invalidatedByBiometricEnrollment = true;   // a new fingerprint makes the key unusable
        bool unlockedDeviceRequired = false;
        bool isStrongBoxBacked = false;
    };

    // Makes (or replaces) the key; false when the Keystore refuses the spec (StrongBox missing,
    // no secure lock screen for an authentication-bound key) or off Android.
    bool GenerateKey(const std::string& alias, const KeyGenSpec& spec);
    bool DeleteKey(const std::string& alias);
    bool ContainsKey(const std::string& alias);
    // An EC or RSA key's public half (X.509 SubjectPublicKeyInfo), to verify its signatures
    // elsewhere or as a key agreement's peer key.
    std::optional<std::vector<std::uint8_t>> GetPublicKey(const std::string& alias);

    struct KeyStoreHost {
        kiln::Function<bool(const std::string& alias, const KeyGenSpec& spec)> generate;
        kiln::Function<bool(const std::string& alias)> remove;
        kiln::Function<bool(const std::string& alias)> contains;
        kiln::Function<std::optional<std::vector<std::uint8_t>>(const std::string& alias)> publicKey;
    };
    void SetKeyStoreHost(KeyStoreHost host);

}
