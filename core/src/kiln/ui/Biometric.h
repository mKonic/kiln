#pragma once

// The biometric prompt (androidx.biometric over android.hardware.biometrics.BiometricPrompt and
// BiometricManager): the system's own dialog asks for a fingerprint, the face or the device's
// PIN. Needs android.permission.USE_BIOMETRIC.
//
//   auto r = co_await ui::Authenticate({ .title = "Unlock notes", .negativeButtonText = "Cancel" });
//   if (r.succeeded) ...
//
// A CryptoObject ties the prompt to a Keystore key made with userAuthenticationRequired
// (security::GenerateKey): once the user authenticates, the key's operation (encrypt, decrypt,
// sign, MAC, key agreement) runs on the given input, and the result carries its output. Kotlin
// apps hold the unlocked Cipher or Signature; kiln runs the operation for the app, since the
// unlocked object cannot leave the platform.
//
// AuthenticationRequest, AuthenticationResult and RegisterForAuthenticationResult are
// androidx.biometric's newer API: a request for a biometric (with fallbacks) or for the device
// credential, a launcher, and one callback for the outcome.

#include "kiln/runtime/Task.h"
#include <kiln/Function.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kiln::lifecycle {
    class LifecycleOwner;
}

namespace kiln::ui {

    // BiometricManager.Authenticators.
    inline constexpr int kBiometricStrong = 0x000F, kBiometricWeak = 0x00FF, kDeviceCredential = 0x8000, kIdentityCheck = 0x10000;

    // BiometricManager.canAuthenticate's results.
    inline constexpr int BIOMETRIC_SUCCESS = 0, BIOMETRIC_STATUS_UNKNOWN = -1, BIOMETRIC_ERROR_UNSUPPORTED = -2, BIOMETRIC_ERROR_HW_UNAVAILABLE = 1,
                         BIOMETRIC_ERROR_NONE_ENROLLED = 11, BIOMETRIC_ERROR_NO_HARDWARE = 12, BIOMETRIC_ERROR_SECURITY_UPDATE_REQUIRED = 15,
                         BIOMETRIC_ERROR_IDENTITY_CHECK_NOT_ACTIVE = 20;

    // BiometricPrompt's errors and authentication types.
    inline constexpr int ERROR_HW_UNAVAILABLE = 1, ERROR_UNABLE_TO_PROCESS = 2, ERROR_TIMEOUT = 3, ERROR_NO_SPACE = 4, ERROR_CANCELED = 5,
                         ERROR_LOCKOUT = 7, ERROR_VENDOR = 8, ERROR_LOCKOUT_PERMANENT = 9, ERROR_USER_CANCELED = 10, ERROR_NO_BIOMETRICS = 11,
                         ERROR_HW_NOT_PRESENT = 12, ERROR_NEGATIVE_BUTTON = 13, ERROR_NO_DEVICE_CREDENTIAL = 14,
                         ERROR_SECURITY_UPDATE_REQUIRED = 15, ERROR_IDENTITY_CHECK_NOT_ACTIVE = 20, ERROR_CONTENT_VIEW_MORE_OPTIONS_BUTTON = 22;
    // kiln's: the CryptoObject's key could not be used (gone, or invalidated by a new
    // enrollment: KeyPermanentlyInvalidatedException), where Kotlin's Cipher.init would throw.
    inline constexpr int ERROR_CRYPTO_UNAVAILABLE = -100;
    inline constexpr int AUTHENTICATION_RESULT_TYPE_UNKNOWN = -1, AUTHENTICATION_RESULT_TYPE_DEVICE_CREDENTIAL = 1, AUTHENTICATION_RESULT_TYPE_BIOMETRIC = 2;

    // BiometricPrompt.CryptoObject: a Keystore key's operation, unlocked by the prompt.
    struct CryptoObject {
        enum class Kind { Cipher, Signature, Mac, KeyAgreement, OperationHandle };
        enum class Mode { Encrypt, Decrypt };
        Kind kind = Kind::Cipher;
        std::string keyAlias;     // the Android Keystore key
        // The transformation or algorithm: "AES/GCM/NoPadding", "SHA256withECDSA", "HmacSHA256", "ECDH".
        std::string algorithm;
        Mode mode = Mode::Encrypt;                // Cipher
        std::vector<std::uint8_t> iv;             // Cipher decrypt: the IV (GCM: the nonce) encryption gave
        std::vector<std::uint8_t> associatedData; // Cipher (AEAD)
        std::vector<std::uint8_t> input;          // what the operation runs on once unlocked
        std::vector<std::uint8_t> peerPublicKey;  // KeyAgreement: the peer's key (X.509)
        std::int64_t operationHandle = 0;         // OperationHandle (API 35): a Keystore operation of the app's own
        // After authentication: the operation's output (ciphertext with its tag, plaintext,
        // signature, MAC, shared secret) and, for encryption, the IV the Keystore chose.
        std::vector<std::uint8_t> output, outputIv;

        static CryptoObject Encrypt(std::string keyAlias, std::vector<std::uint8_t> plain, std::string transformation = "AES/GCM/NoPadding");
        static CryptoObject Decrypt(std::string keyAlias, std::vector<std::uint8_t> sealed, std::vector<std::uint8_t> iv,
                                    std::string transformation = "AES/GCM/NoPadding");
        static CryptoObject Sign(std::string keyAlias, std::vector<std::uint8_t> data, std::string algorithm = "SHA256withECDSA");
        static CryptoObject MacOf(std::string keyAlias, std::vector<std::uint8_t> data, std::string algorithm = "HmacSHA256");
        static CryptoObject Agree(std::string keyAlias, std::vector<std::uint8_t> peerPublicKey, std::string algorithm = "ECDH");
        static CryptoObject Operation(std::int64_t operationHandle);
        bool operator==(const CryptoObject&) const = default;
    };

    // PromptContentItemPlainText / PromptContentItemBulletedText.
    struct PromptContentItem {
        enum class Kind { PlainText, BulletedText };
        Kind kind = Kind::PlainText;
        std::string text;
        bool operator==(const PromptContentItem&) const = default;
    };
    inline PromptContentItem PromptContentItemPlainText(std::string text) { return { PromptContentItem::Kind::PlainText, std::move(text) }; }
    inline PromptContentItem PromptContentItemBulletedText(std::string text) { return { PromptContentItem::Kind::BulletedText, std::move(text) }; }
    // PromptContentView (API 35): a vertical list of items, or a description with a "more
    // options" button (which closes the prompt with ERROR_CONTENT_VIEW_MORE_OPTIONS_BUTTON).
    struct PromptContentView {
        enum class Kind { VerticalList, MoreOptionsButton };
        Kind kind = Kind::VerticalList;
        std::string description;
        std::vector<PromptContentItem> items;
        bool operator==(const PromptContentView&) const = default;
    };
    // PromptVerticalListContentView.Builder (the platform refuses more than 20 items, items past
    // 640 characters and a description past 225: the prompt then fails to show).
    struct PromptVerticalListContentView {
        PromptContentView view{ PromptContentView::Kind::VerticalList, {}, {} };
        PromptVerticalListContentView& SetDescription(std::string d);
        PromptVerticalListContentView& AddListItem(PromptContentItem item);
        PromptVerticalListContentView& AddListItem(PromptContentItem item, int index);
        PromptContentView Build() const { return view; }
    };
    inline PromptContentView PromptContentViewWithMoreOptionsButton(std::string description = {}) {
        return { PromptContentView::Kind::MoreOptionsButton, std::move(description), {} };
    }

    // A fallback offered beside the biometric (AuthenticationRequest.Biometric.Fallback): the
    // device credential, or an option of the app's (CustomFallbackSelected when chosen).
    struct Fallback {
        enum class Kind { DeviceCredential, CustomOption };
        Kind kind = Kind::CustomOption;
        std::string text;
        int iconType = 3;
        static constexpr int ICON_TYPE_PASSWORD = 0, ICON_TYPE_QR_CODE = 1, ICON_TYPE_ACCOUNT = 2, ICON_TYPE_GENERIC = 3;
        static Fallback DeviceCredential() { return { Kind::DeviceCredential, {}, ICON_TYPE_GENERIC }; }
        static Fallback CustomOption(std::string text, int iconType = ICON_TYPE_GENERIC) { return { Kind::CustomOption, std::move(text), iconType }; }
        bool operator==(const Fallback&) const = default;
    };

    // BiometricPrompt.PromptInfo.
    struct BiometricPromptInfo {
        std::string title, subtitle, description;
        // The dismiss button (ERROR_NEGATIVE_BUTTON); not shown (and not allowed) when
        // kDeviceCredential is allowed, where the system offers the PIN instead.
        std::string negativeButtonText = "Cancel";
        int allowedAuthenticators = kBiometricWeak;
        bool confirmationRequired = true;   // a face match waits for a tap
        // API 35, with SET_BIOMETRIC_DIALOG_ADVANCED (granted to system apps only): a logo (a
        // drawable by name or encoded bytes) and its description.
        std::string logoRes;
        std::vector<std::uint8_t> logoBitmap;
        std::string logoDescription;
        std::optional<PromptContentView> contentView;   // API 35
        // What the negative button does: ERROR_NEGATIVE_BUTTON (BiometricPrompt), cancelling
        // (ERROR_CANCELED, the request API's default Cancel), or choosing the first fallback
        // (the request API's single custom option).
        enum class NegativeButton { Error, Cancel, Fallback };
        NegativeButton negativeButton = NegativeButton::Error;
        // More than one custom fallback (API 36.1): the system lists them.
        std::vector<Fallback> fallbackOptions;
        // setDeviceCredentialAllowed (deprecated): kDeviceCredential among the authenticators.
        bool IsDeviceCredentialAllowed() const { return (allowedAuthenticators & kDeviceCredential) != 0; }
        BiometricPromptInfo& SetDeviceCredentialAllowed(bool allowed) {
            allowedAuthenticators = allowed ? (allowedAuthenticators | kDeviceCredential) : (allowedAuthenticators & ~kDeviceCredential);
            return *this;
        }
    };

    struct BiometricResult {
        bool succeeded = false;
        // ERROR_* when it did not succeed.
        int errorCode = 0;
        std::string errorMessage;
        // AuthenticationResult.getAuthenticationType: AUTHENTICATION_RESULT_TYPE_*.
        int authenticationType = 0;
        std::optional<CryptoObject> crypto;   // with its output
    };

    // BiometricManager.canAuthenticate: BIOMETRIC_SUCCESS or BIOMETRIC_ERROR_* (unsupported off Android).
    int CanAuthenticate(int authenticators = kBiometricWeak);
    // BiometricManager.Strings (API 31): the system's names for the authenticators, for the
    // app's own buttons and settings; none off Android.
    struct BiometricStrings {
        std::optional<std::string> buttonLabel, promptMessage, settingName;
    };
    std::optional<BiometricStrings> GetBiometricStrings(int authenticators);
    // Shows the prompt; the result arrives when it closes. Cancelling the task closes it.
    rt::Task<BiometricResult> Authenticate(BiometricPromptInfo info, std::optional<CryptoObject> crypto = std::nullopt);

    // BiometricPrompt with its AuthenticationCallback: onAuthenticationFailed for each rejected
    // attempt (the prompt stays), then one of error or succeeded.
    class BiometricPrompt {
    public:
        struct AuthenticationCallback {
            kiln::Function<void(int errorCode, const std::string& errString)> onAuthenticationError;
            kiln::Function<void(const BiometricResult& result)> onAuthenticationSucceeded;
            kiln::Function<void()> onAuthenticationFailed;
            kiln::Function<void(const Fallback& fallback)> onFallbackSelected;   // the request API's
        };
        explicit BiometricPrompt(AuthenticationCallback callback);
        ~BiometricPrompt();
        BiometricPrompt(const BiometricPrompt&) = delete;
        BiometricPrompt& operator=(const BiometricPrompt&) = delete;
        void Authenticate(BiometricPromptInfo info, std::optional<CryptoObject> crypto = std::nullopt);
        // cancelAuthentication: closes the prompt (ERROR_CANCELED reaches the callback).
        void CancelAuthentication();

    private:
        struct State;
        std::shared_ptr<State> m_State;
    };

    // --- the request API -------------------------------------------------------------------------

    struct AuthenticationRequest {
        // The prompt's body: a description, a vertical list or a "more options" button.
        struct BodyContent {
            enum class Kind { PlainText, VerticalList, ContentViewWithMoreOptionsButton };
            Kind kind = Kind::PlainText;
            std::optional<std::string> description;
            std::vector<PromptContentItem> items;
            static BodyContent PlainText(std::string description) { return { Kind::PlainText, std::move(description), {} }; }
            static BodyContent VerticalList(std::optional<std::string> description = std::nullopt, std::vector<PromptContentItem> items = {}) {
                return { Kind::VerticalList, std::move(description), std::move(items) };
            }
            static BodyContent ContentViewWithMoreOptionsButton(std::optional<std::string> description = std::nullopt) {
                return { Kind::ContentViewWithMoreOptionsButton, std::move(description), {} };
            }
        };
        // A biometric of at least a strength (Class2: weak; Class3: strong, with an optional
        // CryptoObject), with up to MaxFallbackOptions fallbacks (none: a Cancel button).
        struct Biometric {
            struct Strength {
                bool class3 = false;
                std::optional<CryptoObject> cryptoObject;
                static Strength Class2() { return {}; }
                static Strength Class3(std::optional<CryptoObject> crypto = std::nullopt) { return { true, std::move(crypto) }; }
            };
            std::string title;
            std::vector<Fallback> authFallbacks;
            Strength minStrength;
            std::optional<std::string> subtitle;
            std::optional<BodyContent> content;
            bool isConfirmationRequired = true;
            std::vector<std::uint8_t> logoBitmap;   // SET_BIOMETRIC_DIALOG_ADVANCED
            std::string logoRes;
            std::optional<std::string> logoDescription;
            static int GetMaxFallbackOptions() { return 4; }
            // Throws std::invalid_argument for more than one DeviceCredential or too many fallbacks.
            void Validate() const;
        };
        // The device credential alone (API 30), with an optional CryptoObject.
        struct Credential {
            std::string title;
            std::optional<std::string> subtitle;
            std::optional<BodyContent> content;
            std::optional<CryptoObject> cryptoObject;
        };
        std::variant<Biometric, Credential> request;
        AuthenticationRequest(Biometric b) : request(std::move(b)) {}
        AuthenticationRequest(Credential c) : request(std::move(c)) {}
        static AuthenticationRequest BiometricRequest(std::string title, std::vector<Fallback> authFallbacks, kiln::Function<void(Biometric&)> init = {});
        static AuthenticationRequest CredentialRequest(std::string title, kiln::Function<void(Credential&)> init = {});
        // The prompt this request shows (AuthenticationResultRegistry.authInternal); several
        // fallbacks need the platform's fallback list (API 36.1), else the first is used.
        BiometricPromptInfo ToPromptInfo(bool multipleFallbackOptionsSupported, std::optional<CryptoObject>* crypto = nullptr) const;
    };

    struct AuthenticationResult {
        struct Success {
            std::optional<CryptoObject> crypto;
            int authType = AUTHENTICATION_RESULT_TYPE_UNKNOWN;
        };
        struct Error {
            int errorCode = 0;
            std::string errString;
        };
        struct CustomFallbackSelected {
            Fallback fallback;
        };
        std::variant<Success, Error, CustomFallbackSelected> value;
        bool IsSuccess() const { return std::holds_alternative<Success>(value); }
        bool IsError() const { return std::holds_alternative<Error>(value); }
        bool IsCustomFallbackSelected() const { return std::holds_alternative<CustomFallbackSelected>(value); }
        const Success* success() const { return std::get_if<Success>(&value); }
        const Error* error() const { return std::get_if<Error>(&value); }
        const CustomFallbackSelected* customFallbackSelected() const { return std::get_if<CustomFallbackSelected>(&value); }
    };

    struct AuthenticationResultCallback {
        kiln::Function<void(const AuthenticationResult&)> onAuthResult;
        kiln::Function<void()> onAuthAttemptFailed;
    };
    class AuthenticationResultLauncher {
    public:
        virtual ~AuthenticationResultLauncher() = default;
        // launch: shown now when the owner is started, else once it starts; ignored once the
        // owner is destroyed or the launcher unregistered.
        virtual void Launch(AuthenticationRequest input) = 0;
        virtual void Cancel() = 0;
    };
    // registerForAuthenticationResult: a launcher tied to a lifecycle (the activity's by default).
    std::shared_ptr<AuthenticationResultLauncher> RegisterForAuthenticationResult(AuthenticationResultCallback resultCallback);
    std::shared_ptr<AuthenticationResultLauncher> RegisterForAuthenticationResult(lifecycle::LifecycleOwner& owner, AuthenticationResultCallback resultCallback);

    // --- the platform's ------------------------------------------------------------------------

    struct BiometricEvent {
        enum class Kind { Succeeded, Error, Failed, FallbackSelected };
        Kind kind = Kind::Error;
        int errorCode = 0;
        std::string message;
        int authenticationType = 0;
        int fallbackIndex = 0;                 // FallbackSelected: into fallbackOptions (0 for the negative button)
        std::optional<CryptoObject> crypto;    // Succeeded
    };
    struct BiometricHost {
        kiln::Function<int(int authenticators)> canAuthenticate;
        kiln::Function<std::optional<BiometricStrings>(int authenticators)> strings;
        // Calls back for each event until a final one; returns an id for cancel (-1: not shown).
        kiln::Function<int(const BiometricPromptInfo&, const std::optional<CryptoObject>&, kiln::Function<void(const BiometricEvent&)>)> authenticate;
        kiln::Function<void(int id)> cancel;
        // multipleFallbackOptionsValid's API check (36.1).
        kiln::Function<bool()> multipleFallbackOptions;
    };
    void SetBiometricHost(BiometricHost host);

}
