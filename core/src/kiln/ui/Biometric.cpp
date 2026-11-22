#include "kiln/ui/Biometric.h"

#include "kiln/Log.h"
#include "kiln/lifecycle/Lifecycle.h"
#include "kiln/lifecycle/ProcessLifecycleOwner.h"
#include "kiln/runtime/Channel.h"

#include <memory>
#include <stdexcept>

namespace kiln::ui {

    namespace {
        BiometricHost& Host() { static auto* h = new BiometricHost; return *h; }
    }

    // --- crypto objects ---------------------------------------------------------------------------

    CryptoObject CryptoObject::Encrypt(std::string keyAlias, std::vector<std::uint8_t> plain, std::string transformation) {
        CryptoObject c;
        c.kind = Kind::Cipher;
        c.keyAlias = std::move(keyAlias);
        c.algorithm = std::move(transformation);
        c.mode = Mode::Encrypt;
        c.input = std::move(plain);
        return c;
    }
    CryptoObject CryptoObject::Decrypt(std::string keyAlias, std::vector<std::uint8_t> sealed, std::vector<std::uint8_t> iv, std::string transformation) {
        CryptoObject c = Encrypt(std::move(keyAlias), std::move(sealed), std::move(transformation));
        c.mode = Mode::Decrypt;
        c.iv = std::move(iv);
        return c;
    }
    CryptoObject CryptoObject::Sign(std::string keyAlias, std::vector<std::uint8_t> data, std::string algorithm) {
        CryptoObject c;
        c.kind = Kind::Signature;
        c.keyAlias = std::move(keyAlias);
        c.algorithm = std::move(algorithm);
        c.input = std::move(data);
        return c;
    }
    CryptoObject CryptoObject::MacOf(std::string keyAlias, std::vector<std::uint8_t> data, std::string algorithm) {
        CryptoObject c = Sign(std::move(keyAlias), std::move(data), std::move(algorithm));
        c.kind = Kind::Mac;
        return c;
    }
    CryptoObject CryptoObject::Agree(std::string keyAlias, std::vector<std::uint8_t> peerPublicKey, std::string algorithm) {
        CryptoObject c;
        c.kind = Kind::KeyAgreement;
        c.keyAlias = std::move(keyAlias);
        c.algorithm = std::move(algorithm);
        c.peerPublicKey = std::move(peerPublicKey);
        return c;
    }
    CryptoObject CryptoObject::Operation(std::int64_t operationHandle) {
        CryptoObject c;
        c.kind = Kind::OperationHandle;
        c.operationHandle = operationHandle;
        return c;
    }

    PromptVerticalListContentView& PromptVerticalListContentView::SetDescription(std::string d) {
        view.description = std::move(d);
        return *this;
    }
    PromptVerticalListContentView& PromptVerticalListContentView::AddListItem(PromptContentItem item) {
        view.items.push_back(std::move(item));
        return *this;
    }
    PromptVerticalListContentView& PromptVerticalListContentView::AddListItem(PromptContentItem item, int index) {
        if (index < 0 || std::size_t(index) > view.items.size()) throw std::out_of_range("index " + std::to_string(index) + " out of the list's bounds");
        view.items.insert(view.items.begin() + index, std::move(item));
        return *this;
    }

    // --- the prompt -----------------------------------------------------------------------------

    int CanAuthenticate(int authenticators) { return Host().canAuthenticate ? Host().canAuthenticate(authenticators) : BIOMETRIC_ERROR_UNSUPPORTED; }
    std::optional<BiometricStrings> GetBiometricStrings(int authenticators) {
        return Host().strings ? Host().strings(authenticators) : std::nullopt;
    }

    rt::Task<BiometricResult> Authenticate(BiometricPromptInfo info, std::optional<CryptoObject> crypto) {
        if (!Host().authenticate) co_return BiometricResult{ false, BIOMETRIC_ERROR_UNSUPPORTED, "unsupported", 0, std::nullopt };
        auto result = std::make_shared<rt::Channel<BiometricResult>>(1);
        struct Prompt {
            int id = -1;
            ~Prompt() { if (id >= 0 && Host().cancel) Host().cancel(id); }
        } prompt;
        prompt.id = Host().authenticate(info, crypto, [result](const BiometricEvent& e) {
            switch (e.kind) {
                case BiometricEvent::Kind::Failed: break;   // a rejected attempt: the prompt stays
                case BiometricEvent::Kind::Succeeded: result->TrySend(BiometricResult{ true, 0, {}, e.authenticationType, e.crypto }); break;
                case BiometricEvent::Kind::Error: result->TrySend(BiometricResult{ false, e.errorCode, e.message, 0, std::nullopt }); break;
                case BiometricEvent::Kind::FallbackSelected:
                    result->TrySend(BiometricResult{ false, ERROR_NEGATIVE_BUTTON, e.message, 0, std::nullopt });
                    break;
            }
        });
        if (prompt.id < 0) co_return BiometricResult{ false, ERROR_HW_UNAVAILABLE, "unavailable", 0, std::nullopt };
        auto got = co_await result->Next();
        prompt.id = -1;
        co_return got ? *got : BiometricResult{ false, ERROR_CANCELED, "cancelled", 0, std::nullopt };
    }

    struct BiometricPrompt::State {
        AuthenticationCallback callback;
        int id = -1;
        BiometricPromptInfo info;
    };
    BiometricPrompt::BiometricPrompt(AuthenticationCallback callback) : m_State(std::make_shared<State>()) { m_State->callback = std::move(callback); }
    BiometricPrompt::~BiometricPrompt() {
        // The prompt goes with its owner; nothing further reaches the callback.
        if (m_State->id >= 0 && Host().cancel) Host().cancel(m_State->id);
        m_State->callback = {};
    }
    void BiometricPrompt::Authenticate(BiometricPromptInfo info, std::optional<CryptoObject> crypto) {
        auto state = m_State;
        state->info = info;
        if (!Host().authenticate) {
            if (state->callback.onAuthenticationError) state->callback.onAuthenticationError(BIOMETRIC_ERROR_UNSUPPORTED, "unsupported");
            return;
        }
        std::weak_ptr<State> weak = state;
        state->id = Host().authenticate(info, crypto, [weak](const BiometricEvent& e) {
            auto s = weak.lock();
            if (!s) return;
            AuthenticationCallback& cb = s->callback;
            if (e.kind != BiometricEvent::Kind::Failed) s->id = -1;
            switch (e.kind) {
                case BiometricEvent::Kind::Failed:
                    if (cb.onAuthenticationFailed) cb.onAuthenticationFailed();
                    break;
                case BiometricEvent::Kind::Succeeded:
                    if (cb.onAuthenticationSucceeded) cb.onAuthenticationSucceeded(BiometricResult{ true, 0, {}, e.authenticationType, e.crypto });
                    break;
                case BiometricEvent::Kind::Error:
                    if (cb.onAuthenticationError) cb.onAuthenticationError(e.errorCode, e.message);
                    break;
                case BiometricEvent::Kind::FallbackSelected: {
                    const auto& options = s->info.fallbackOptions;
                    Fallback f = e.fallbackIndex >= 0 && std::size_t(e.fallbackIndex) < options.size() ? options[std::size_t(e.fallbackIndex)]
                                                                                                         : Fallback::CustomOption(e.message);
                    if (cb.onFallbackSelected) cb.onFallbackSelected(f);
                    break;
                }
            }
        });
        if (state->id < 0 && state->callback.onAuthenticationError) state->callback.onAuthenticationError(ERROR_HW_UNAVAILABLE, "unavailable");
    }
    void BiometricPrompt::CancelAuthentication() {
        if (m_State->id >= 0 && Host().cancel) Host().cancel(m_State->id);
    }

    // --- the request API -----------------------------------------------------------------------

    void AuthenticationRequest::Biometric::Validate() const {
        int credentials = 0;
        for (const Fallback& f : authFallbacks) credentials += f.kind == Fallback::Kind::DeviceCredential;
        if (credentials > 1) throw std::invalid_argument("At most one DeviceCredential can be provided as a fallback option.");
        if (int(authFallbacks.size()) > GetMaxFallbackOptions())
            throw std::invalid_argument("Maximum fallback option count exceeded. A maximum of " + std::to_string(GetMaxFallbackOptions()) + " fallbacks are allowed.");
    }
    AuthenticationRequest AuthenticationRequest::BiometricRequest(std::string title, std::vector<Fallback> authFallbacks, kiln::Function<void(Biometric&)> init) {
        Biometric b;
        b.title = std::move(title);
        b.authFallbacks = std::move(authFallbacks);
        if (init) init(b);
        b.Validate();
        return AuthenticationRequest(std::move(b));
    }
    AuthenticationRequest AuthenticationRequest::CredentialRequest(std::string title, kiln::Function<void(Credential&)> init) {
        Credential c;
        c.title = std::move(title);
        if (init) init(c);
        return AuthenticationRequest(std::move(c));
    }

    namespace {
        void ApplyContent(BiometricPromptInfo& info, const std::optional<AuthenticationRequest::BodyContent>& content) {
            if (!content) return;
            using K = AuthenticationRequest::BodyContent::Kind;
            switch (content->kind) {
                case K::PlainText: info.description = content->description.value_or(""); break;
                case K::VerticalList: info.contentView = PromptContentView{ PromptContentView::Kind::VerticalList, content->description.value_or(""), content->items }; break;
                case K::ContentViewWithMoreOptionsButton:
                    info.contentView = PromptContentViewWithMoreOptionsButton(content->description.value_or(""));
                    break;
            }
        }
    }

    BiometricPromptInfo AuthenticationRequest::ToPromptInfo(bool multipleSupported, std::optional<CryptoObject>* crypto) const {
        BiometricPromptInfo info;
        info.negativeButtonText.clear();
        if (const Biometric* b = std::get_if<Biometric>(&request)) {
            info.title = b->title;
            info.subtitle = b->subtitle.value_or("");
            int authType = b->minStrength.class3 ? kBiometricStrong : kBiometricWeak;
            // multipleFallbackOptionsValid: several fallbacks need API 36.1; otherwise the first,
            // or a Cancel button.
            std::vector<Fallback> fallbacks;
            const bool multiple = multipleSupported && b->authFallbacks.size() > 1;
            if (multiple) fallbacks = b->authFallbacks;
            else if (!b->authFallbacks.empty()) fallbacks = { b->authFallbacks.front() };
            if (fallbacks.empty()) {
                info.negativeButton = BiometricPromptInfo::NegativeButton::Cancel;   // the platform's "Cancel"
            }
            for (const Fallback& f : fallbacks)
                if (f.kind == Fallback::Kind::DeviceCredential) authType |= kDeviceCredential;
            if (fallbacks.size() == 1 && fallbacks[0].kind == Fallback::Kind::CustomOption) {
                info.negativeButtonText = fallbacks[0].text;
                info.negativeButton = BiometricPromptInfo::NegativeButton::Fallback;
                info.fallbackOptions = fallbacks;
            } else if (fallbacks.size() > 1) {
                for (const Fallback& f : fallbacks)
                    if (f.kind == Fallback::Kind::CustomOption) info.fallbackOptions.push_back(f);
            }
            info.allowedAuthenticators = authType;
            ApplyContent(info, b->content);
            if (!b->logoRes.empty()) info.logoRes = b->logoRes;
            else if (!b->logoBitmap.empty()) info.logoBitmap = b->logoBitmap;
            info.logoDescription = b->logoDescription.value_or("");
            info.confirmationRequired = b->isConfirmationRequired;
            if (crypto) *crypto = b->minStrength.class3 ? b->minStrength.cryptoObject : std::nullopt;
        } else {
            const Credential& c = std::get<Credential>(request);
            info.title = c.title;
            info.subtitle = c.subtitle.value_or("");
            info.allowedAuthenticators = kDeviceCredential;
            ApplyContent(info, c.content);
            if (crypto) *crypto = c.cryptoObject;
        }
        return info;
    }

    namespace {
        class Launcher : public AuthenticationResultLauncher, public std::enable_shared_from_this<Launcher> {
        public:
            Launcher(AuthenticationResultCallback callback) : m_Callback(std::move(callback)) {}
            ~Launcher() override { Unregister(); }

            void Attach(lifecycle::Lifecycle& lifecycle) {
                m_Lifecycle = &lifecycle;
                std::weak_ptr<Launcher> weak = shared_from_this();
                m_Observer = lifecycle::AddObserver(lifecycle, [weak](lifecycle::LifecycleOwner&, lifecycle::Lifecycle::Event event) {
                    auto self = weak.lock();
                    if (!self) return;
                    if (event == lifecycle::Lifecycle::Event::OnStart) {
                        if (self->m_Pending) {
                            AuthenticationRequest r = std::move(*self->m_Pending);
                            self->m_Pending.reset();
                            self->Show(std::move(r));
                        }
                    } else if (event == lifecycle::Lifecycle::Event::OnDestroy) {
                        self->m_Pending.reset();
                        self->m_Destroyed = true;
                        self->m_Prompt.reset();
                    }
                });
            }
            void Launch(AuthenticationRequest input) override {
                if (m_Unregistered) {
                    log::Warn("kiln", "Ignoring launch() on an unregistered AuthenticationResultLauncher.");
                    return;
                }
                const auto state = m_Lifecycle ? m_Lifecycle->CurrentState() : lifecycle::Lifecycle::State::Resumed;
                if (m_Destroyed || state == lifecycle::Lifecycle::State::Destroyed) {
                    log::Warn("kiln", "Ignoring launch() on a destroyed LifecycleOwner.");
                    return;
                }
                if (lifecycle::IsAtLeast(state, lifecycle::Lifecycle::State::Started)) Show(std::move(input));
                else m_Pending = std::move(input);
            }
            void Cancel() override {
                m_Pending.reset();
                if (m_Prompt) m_Prompt->CancelAuthentication();
            }
            void Unregister() {
                m_Unregistered = true;
                m_Pending.reset();
                if (m_Lifecycle && m_Observer) m_Lifecycle->RemoveObserver(m_Observer);
                m_Observer.reset();
                m_Prompt.reset();
            }

        private:
            void Show(AuthenticationRequest input) {
                std::optional<CryptoObject> crypto;
                BiometricPromptInfo info = input.ToPromptInfo(Host().multipleFallbackOptions && Host().multipleFallbackOptions(), &crypto);
                std::weak_ptr<Launcher> weak = shared_from_this();
                BiometricPrompt::AuthenticationCallback cb;
                cb.onAuthenticationFailed = [weak] {
                    if (auto s = weak.lock(); s && s->m_Callback.onAuthAttemptFailed) s->m_Callback.onAuthAttemptFailed();
                };
                cb.onAuthenticationSucceeded = [weak](const BiometricResult& r) {
                    if (auto s = weak.lock(); s && s->m_Callback.onAuthResult) s->m_Callback.onAuthResult({ AuthenticationResult::Success{ r.crypto, r.authenticationType } });
                };
                cb.onAuthenticationError = [weak](int code, const std::string& message) {
                    if (auto s = weak.lock(); s && s->m_Callback.onAuthResult) s->m_Callback.onAuthResult({ AuthenticationResult::Error{ code, message } });
                };
                cb.onFallbackSelected = [weak](const Fallback& f) {
                    if (auto s = weak.lock(); s && s->m_Callback.onAuthResult) s->m_Callback.onAuthResult({ AuthenticationResult::CustomFallbackSelected{ f } });
                };
                if (!m_Prompt) m_Prompt = std::make_unique<BiometricPrompt>(std::move(cb));
                else {
                    m_Prompt.reset();
                    m_Prompt = std::make_unique<BiometricPrompt>(std::move(cb));
                }
                m_Prompt->Authenticate(std::move(info), std::move(crypto));
            }

            AuthenticationResultCallback m_Callback;
            lifecycle::Lifecycle* m_Lifecycle = nullptr;
            std::shared_ptr<lifecycle::LifecycleEventObserver> m_Observer;
            std::optional<AuthenticationRequest> m_Pending;
            std::unique_ptr<BiometricPrompt> m_Prompt;
            bool m_Unregistered = false, m_Destroyed = false;
        };
    }

    std::shared_ptr<AuthenticationResultLauncher> RegisterForAuthenticationResult(lifecycle::LifecycleOwner& owner, AuthenticationResultCallback resultCallback) {
        auto l = std::make_shared<Launcher>(std::move(resultCallback));
        l->Attach(owner.GetLifecycle());
        return l;
    }
    std::shared_ptr<AuthenticationResultLauncher> RegisterForAuthenticationResult(AuthenticationResultCallback resultCallback) {
        return RegisterForAuthenticationResult(lifecycle::ProcessLifecycleOwner::Get(), std::move(resultCallback));
    }

    void SetBiometricHost(BiometricHost host) { Host() = std::move(host); }

}
