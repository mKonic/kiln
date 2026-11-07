#pragma once

// Credential Manager requests on autofill nodes (SemanticsPropertiesAndroid.CredentialRequest with
// CredentialRequestData): an android.credentials.GetCredentialRequest the autofill structure
// hands the platform (ViewStructure.setPendingCredentialRequest, API 35), and the callback that
// receives the chosen credential. The request mirrors the platform's (options with Bundle data);
// GetPasswordOption and GetPublicKeyCredentialOption build the bundles androidx.credentials does.

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace kiln::ui {

    // A Uri (Parcelable) in a Bundle or an Intent's extras.
    struct BundleUri {
        std::string uri;
        bool operator==(const BundleUri&) const = default;
    };
    // The Bundle values kiln passes to and from the platform (a null byte array is an empty
    // optional): strings, booleans, ints, string lists / arrays, byte arrays, URIs, int arrays
    // and longs.
    using BundleValue = std::variant<std::string, bool, std::int32_t, std::vector<std::string>, std::optional<std::vector<std::uint8_t>>, BundleUri,
                                     std::vector<std::int32_t>, std::int64_t>;

    struct Bundle {
        std::vector<std::pair<std::string, BundleValue>> entries;
        void Put(std::string key, BundleValue value) {
            for (auto& [k, v] : entries)
                if (k == key) { v = std::move(value); return; }
            entries.emplace_back(std::move(key), std::move(value));
        }
        const BundleValue* Get(const std::string& key) const {
            for (const auto& [k, v] : entries) if (k == key) return &v;
            return nullptr;
        }
        const std::string* GetString(const std::string& key) const {
            const BundleValue* v = Get(key);
            return v ? std::get_if<std::string>(v) : nullptr;
        }
        bool operator==(const Bundle&) const = default;
    };

    // android.credentials.CredentialOption.
    struct CredentialOption {
        std::string type;
        Bundle requestData, candidateQueryData;
        bool isSystemProviderRequired = false;
        bool operator==(const CredentialOption&) const = default;
    };

    // android.credentials.Credential.
    struct Credential {
        std::string type;
        Bundle data;
    };

    struct CredentialRequestData {
        std::vector<CredentialOption> options;   // GetCredentialRequest's options, in order
        Bundle data;                             // GetCredentialRequest's data
        std::function<void(const Credential&)> onResult;
        // GetCredentialException's type and message.
        std::function<void(const std::string& type, const std::string& message)> onError;
    };

    namespace credentials {
        inline constexpr const char* kTypePasswordCredential = "android.credentials.TYPE_PASSWORD_CREDENTIAL";
        inline constexpr const char* kTypePublicKeyCredential = "androidx.credentials.TYPE_PUBLIC_KEY_CREDENTIAL";
        inline constexpr const char* kBundleKeyId = "androidx.credentials.BUNDLE_KEY_ID";
        inline constexpr const char* kBundleKeyPassword = "androidx.credentials.BUNDLE_KEY_PASSWORD";
        inline constexpr const char* kBundleKeyAuthenticationResponseJson = "androidx.credentials.BUNDLE_KEY_AUTHENTICATION_RESPONSE_JSON";
        inline constexpr const char* kBundleKeySubtype = "androidx.credentials.BUNDLE_KEY_SUBTYPE";
        inline constexpr const char* kBundleKeyAllowedUserIds = "androidx.credentials.BUNDLE_KEY_ALLOWED_USER_IDS";
        inline constexpr const char* kBundleKeyIsAutoSelectAllowed = "androidx.credentials.BUNDLE_KEY_IS_AUTO_SELECT_ALLOWED";
        inline constexpr const char* kBundleKeyTypePriorityValue = "androidx.credentials.BUNDLE_KEY_TYPE_PRIORITY_VALUE";
        inline constexpr const char* kBundleKeyClientDataHash = "androidx.credentials.BUNDLE_KEY_CLIENT_DATA_HASH";
        inline constexpr const char* kBundleKeyRequestJson = "androidx.credentials.BUNDLE_KEY_REQUEST_JSON";
        inline constexpr const char* kSubtypeGetPublicKeyCredentialOption = "androidx.credentials.BUNDLE_VALUE_SUBTYPE_GET_PUBLIC_KEY_CREDENTIAL_OPTION";
        inline constexpr std::int32_t kPriorityPasskeyOrSimilar = 100, kPriorityOidcOrSimilar = 500, kPriorityPasswordOrSimilar = 1000,
                                      kPriorityDefault = 2000;
    }

    // CredentialOption's init: both bundles carry auto-select and the type priority.
    inline void AddCredentialOptionExtras(CredentialOption& o, bool isAutoSelectAllowed, std::int32_t typePriorityHint) {
        for (Bundle* b : { &o.requestData, &o.candidateQueryData }) {
            b->Put(credentials::kBundleKeyIsAutoSelectAllowed, isAutoSelectAllowed);
            b->Put(credentials::kBundleKeyTypePriorityValue, typePriorityHint);
        }
    }

    // GetPasswordOption.
    inline CredentialOption GetPasswordOption(std::vector<std::string> allowedUserIds = {}, bool isAutoSelectAllowed = false) {
        CredentialOption o;
        o.type = credentials::kTypePasswordCredential;
        o.requestData.Put(credentials::kBundleKeyAllowedUserIds, allowedUserIds);
        o.candidateQueryData.Put(credentials::kBundleKeyAllowedUserIds, std::move(allowedUserIds));
        AddCredentialOptionExtras(o, isAutoSelectAllowed, credentials::kPriorityPasswordOrSimilar);
        return o;
    }

    // GetPublicKeyCredentialOption (a passkey; requestJson is the WebAuthn request).
    inline CredentialOption GetPublicKeyCredentialOption(std::string requestJson, std::optional<std::vector<std::uint8_t>> clientDataHash = std::nullopt) {
        if (requestJson.empty()) throw std::invalid_argument("requestJson must not be empty, and must be a valid JSON");
        CredentialOption o;
        o.type = credentials::kTypePublicKeyCredential;
        for (Bundle* b : { &o.requestData, &o.candidateQueryData }) {
            b->Put(credentials::kBundleKeySubtype, std::string(credentials::kSubtypeGetPublicKeyCredentialOption));
            b->Put(credentials::kBundleKeyRequestJson, requestJson);
            b->Put(credentials::kBundleKeyClientDataHash, clientDataHash);
        }
        AddCredentialOptionExtras(o, true, credentials::kPriorityPasskeyOrSimilar);
        return o;
    }

}
