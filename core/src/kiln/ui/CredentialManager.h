#pragma once

// Credential Manager (androidx.credentials CredentialManager over the platform's
// android.credentials.CredentialManager, API 34+): sign in with a saved password or passkey,
// save a new one, and clear the provider's sign-in state on sign-out. The system shows its
// own sheet over the activity; the result arrives when the user picks or dismisses it.
//
//   try {
//       ui::Credential c = co_await ui::GetCredential({ .credentialOptions = { ui::GetPasswordOption() } });
//       if (c.type == ui::credentials::kTypePasswordCredential) SignIn(*c.data.GetString(ui::credentials::kBundleKeyId), ...);
//   } catch (const ui::GetCredentialException& e) {
//       if (e.Type() != ui::credentials::kGetTypeUserCanceled) ShowError(e.what());
//   }
//
//   co_await ui::CreateCredential(ui::CreatePasswordRequest(user, password));
//
// Below API 34 (and off Android) each call throws its *UnsupportedException type.

#include "kiln/runtime/Task.h"
#include "kiln/ui/Credentials.h"

#include <kiln/Function.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiln::ui {

    namespace credentials {
        // The platform's exception types, and androidx's own.
        inline constexpr const char* kGetTypeNoCredential = "android.credentials.GetCredentialException.TYPE_NO_CREDENTIAL";
        inline constexpr const char* kGetTypeUserCanceled = "android.credentials.GetCredentialException.TYPE_USER_CANCELED";
        inline constexpr const char* kGetTypeInterrupted = "android.credentials.GetCredentialException.TYPE_INTERRUPTED";
        inline constexpr const char* kGetTypeUnknown = "android.credentials.GetCredentialException.TYPE_UNKNOWN";
        inline constexpr const char* kGetTypeUnsupported = "androidx.credentials.TYPE_GET_CREDENTIAL_UNSUPPORTED_EXCEPTION";
        inline constexpr const char* kCreateTypeNoCreateOptions = "android.credentials.CreateCredentialException.TYPE_NO_CREATE_OPTIONS";
        inline constexpr const char* kCreateTypeUserCanceled = "android.credentials.CreateCredentialException.TYPE_USER_CANCELED";
        inline constexpr const char* kCreateTypeInterrupted = "android.credentials.CreateCredentialException.TYPE_INTERRUPTED";
        inline constexpr const char* kCreateTypeUnknown = "android.credentials.CreateCredentialException.TYPE_UNKNOWN";
        inline constexpr const char* kCreateTypeUnsupported = "androidx.credentials.TYPE_CREATE_CREDENTIAL_UNSUPPORTED_EXCEPTION";
        inline constexpr const char* kClearTypeUnknown = "android.credentials.ClearCredentialStateException.TYPE_UNKNOWN";
        inline constexpr const char* kClearTypeUnsupported = "androidx.credentials.TYPE_CLEAR_CREDENTIAL_UNSUPPORTED_EXCEPTION";
        // WebAuthn DOM errors arrive as this prefix and the DOM error name.
        inline constexpr const char* kGetTypePublicKeyDomPrefix = "androidx.credentials.TYPE_GET_PUBLIC_KEY_CREDENTIAL_DOM_EXCEPTION";
        inline constexpr const char* kCreateTypePublicKeyDomPrefix = "androidx.credentials.TYPE_CREATE_PUBLIC_KEY_CREDENTIAL_DOM_EXCEPTION";

        inline constexpr const char* kBundleKeyRequestDisplayInfo = "androidx.credentials.BUNDLE_KEY_REQUEST_DISPLAY_INFO";
        inline constexpr const char* kBundleKeyUserId = "androidx.credentials.BUNDLE_KEY_USER_ID";
        inline constexpr const char* kBundleKeyUserDisplayName = "androidx.credentials.BUNDLE_KEY_USER_DISPLAY_NAME";
        inline constexpr const char* kBundleKeyDefaultProvider = "androidx.credentials.BUNDLE_KEY_DEFAULT_PROVIDER";
        inline constexpr const char* kBundleKeyPreferImmediatelyAvailableCredentials = "androidx.credentials.BUNDLE_KEY_PREFER_IMMEDIATELY_AVAILABLE_CREDENTIALS";
        inline constexpr const char* kBundleKeyPreferIdentityDocUi = "androidx.credentials.BUNDLE_KEY_PREFER_IDENTITY_DOC_UI";
        inline constexpr const char* kBundleKeyIsConditionalRequest = "androidx.credentials.BUNDLE_KEY_IS_CONDITIONAL_REQUEST";
        inline constexpr const char* kSubtypeCreatePublicKeyCredentialRequest = "androidx.credentials.BUNDLE_VALUE_SUBTYPE_CREATE_PUBLIC_KEY_CREDENTIAL_REQUEST";
        inline constexpr const char* kBundleKeyRegistrationResponseJson = "androidx.credentials.BUNDLE_KEY_REGISTRATION_RESPONSE_JSON";
    }

    // GetCredentialException / CreateCredentialException / ClearCredentialException: the
    // platform's (or androidx's) type and message.
    class CredentialException : public std::runtime_error {
    public:
        CredentialException(std::string type, const std::string& message) : std::runtime_error(message), m_Type(std::move(type)) {}
        const std::string& Type() const { return m_Type; }
    private:
        std::string m_Type;
    };
    class GetCredentialException : public CredentialException { using CredentialException::CredentialException; };
    class CreateCredentialException : public CredentialException { using CredentialException::CredentialException; };
    class ClearCredentialException : public CredentialException { using CredentialException::CredentialException; };

    // GetCredentialRequest: the options in order (GetPasswordOption, GetPublicKeyCredentialOption).
    struct GetCredentialRequest {
        std::vector<CredentialOption> credentialOptions;
        // For a browser asking on a site's behalf; needs CREDENTIAL_MANAGER_SET_ORIGIN.
        std::optional<std::string> origin;
        bool preferIdentityDocUi = false;
        bool preferImmediatelyAvailableCredentials = false;
        // getRequestMetadataBundle: the request's data bundle.
        Bundle MetadataBundle() const;
    };

    // CreateCredentialRequest.DisplayInfo.
    struct CreateCredentialDisplayInfo {
        std::string userId;
        std::optional<std::string> userDisplayName, preferDefaultProvider;
        Bundle ToBundle() const;
    };

    // CreateCredentialRequest, as CreatePasswordRequest / CreatePublicKeyCredentialRequest build it.
    struct CreateCredentialRequest {
        std::string type;
        Bundle credentialData, candidateQueryData;
        bool isSystemProviderRequired = false;
        bool isAutoSelectAllowed = false;
        CreateCredentialDisplayInfo displayInfo;
        std::optional<std::string> origin;
        bool preferImmediatelyAvailableCredentials = false;
        // getFinalCreateCredentialData: credentialData with the display info nested in it.
        Bundle FinalCredentialData() const;
    };

    // CreateCredentialRequest's init: auto-select and the immediacy preference in the data.
    // Throws std::invalid_argument for an empty display userId.
    CreateCredentialRequest MakeCreateCredentialRequest(std::string type, Bundle credentialData, Bundle candidateQueryData,
                                                        bool isSystemProviderRequired, bool isAutoSelectAllowed,
                                                        CreateCredentialDisplayInfo displayInfo, std::optional<std::string> origin,
                                                        bool preferImmediatelyAvailableCredentials);

    // CreatePasswordRequest: save id and password. Throws std::invalid_argument for an empty
    // password or id.
    CreateCredentialRequest CreatePasswordRequest(const std::string& id, const std::string& password, std::optional<std::string> origin = std::nullopt,
                                                  bool preferImmediatelyAvailableCredentials = false, bool isAutoSelectAllowed = false,
                                                  std::optional<std::string> preferDefaultProvider = std::nullopt);

    // CreatePublicKeyCredentialRequest: register a passkey. requestJson is WebAuthn's
    // PublicKeyCredentialCreationOptionsJSON; its user.name (and displayName) are shown.
    // Throws std::invalid_argument when it is not JSON or has no user.name.
    CreateCredentialRequest CreatePublicKeyCredentialRequest(const std::string& requestJson,
                                                             std::optional<std::vector<std::uint8_t>> clientDataHash = std::nullopt,
                                                             bool preferImmediatelyAvailableCredentials = false,
                                                             std::optional<std::string> origin = std::nullopt, bool isAutoSelectAllowed = false,
                                                             bool isConditional = false,
                                                             std::optional<std::string> preferDefaultProvider = std::nullopt);

    // CreateCredentialResponse: its type and data (a passkey's registration response JSON is
    // under kBundleKeyRegistrationResponseJson).
    struct CreateCredentialResponse {
        std::string type;
        Bundle data;
    };

    // Shows the sheet; the chosen credential (PasswordCredential: kBundleKeyId and
    // kBundleKeyPassword; a passkey: kBundleKeyAuthenticationResponseJson). Throws
    // GetCredentialException. Cancelling the task closes the sheet.
    rt::Task<Credential> GetCredential(GetCredentialRequest request);
    // Throws CreateCredentialException.
    rt::Task<CreateCredentialResponse> CreateCredential(CreateCredentialRequest request);
    // On sign-out: providers forget the session. Throws ClearCredentialException.
    rt::Task<> ClearCredentialState();

    // What the platform does with the calls; Android installs it. Each call answers once, through
    // the callback, and returns an id for cancel (< 0: unsupported, answered already).
    struct CredentialManagerHost {
        struct Outcome {
            bool ok = false;
            std::string type;   // the credential's or the error's
            Bundle data;
            std::string message;
        };
        kiln::Function<int(const GetCredentialRequest&, kiln::Function<void(Outcome)>)> get;
        kiln::Function<int(const CreateCredentialRequest&, kiln::Function<void(Outcome)>)> create;
        kiln::Function<int(kiln::Function<void(Outcome)>)> clear;
        kiln::Function<void(int id)> cancel;
    };
    void SetCredentialManagerHost(CredentialManagerHost host);

}
