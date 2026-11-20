#include "kiln/ui/CredentialManager.h"

#include "kiln/runtime/Channel.h"

#include <cctype>
#include <map>
#include <memory>

namespace kiln::ui {

    namespace {
        CredentialManagerHost& Host() { static auto* h = new CredentialManagerHost; return *h; }
        constexpr const char* kUnsupported = "Your device doesn't support credential manager";

        // Enough JSON (RFC 8259) to validate a request and read user.name / user.displayName, as
        // JSONTokener and JSONObject do for CreatePublicKeyCredentialRequest.
        struct JsonValue {
            enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
            std::string text;
            std::map<std::string, JsonValue> members;
        };
        class JsonReader {
        public:
            explicit JsonReader(const std::string& s) : m_S(s) {}
            bool Parse(JsonValue& out) {
                if (!Value(out, 0)) return false;
                Space();
                return m_I == m_S.size();
            }
        private:
            void Space() { while (m_I < m_S.size() && (m_S[m_I] == ' ' || m_S[m_I] == '\t' || m_S[m_I] == '\n' || m_S[m_I] == '\r')) ++m_I; }
            bool Literal(const char* word) {
                const std::string_view w(word);
                if (m_S.compare(m_I, w.size(), w) != 0) return false;
                m_I += w.size();
                return true;
            }
            bool String(std::string& out) {
                if (m_I >= m_S.size() || m_S[m_I] != '"') return false;
                ++m_I;
                while (m_I < m_S.size()) {
                    const char c = m_S[m_I++];
                    if (c == '"') return true;
                    if (static_cast<unsigned char>(c) < 0x20) return false;
                    if (c != '\\') { out += c; continue; }
                    if (m_I >= m_S.size()) return false;
                    const char e = m_S[m_I++];
                    switch (e) {
                        case '"': case '\\': case '/': out += e; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case 'u': {
                            auto hex = [&](std::uint32_t& v) {
                                if (m_I + 4 > m_S.size()) return false;
                                v = 0;
                                for (int k = 0; k < 4; ++k) {
                                    const char h = m_S[m_I++];
                                    if (!std::isxdigit(static_cast<unsigned char>(h))) return false;
                                    v = v * 16 + std::uint32_t(std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : (std::tolower(h) - 'a' + 10));
                                }
                                return true;
                            };
                            std::uint32_t cp;
                            if (!hex(cp)) return false;
                            if (cp >= 0xD800 && cp < 0xDC00 && m_S.compare(m_I, 2, "\\u") == 0) {
                                m_I += 2;
                                std::uint32_t lo;
                                if (!hex(lo)) return false;
                                if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            }
                            if (cp < 0x80) out += char(cp);
                            else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
                            else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
                            else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
                            break;
                        }
                        default: return false;
                    }
                }
                return false;
            }
            bool Value(JsonValue& out, int depth) {
                if (depth > 64) return false;
                Space();
                if (m_I >= m_S.size()) return false;
                const char c = m_S[m_I];
                if (c == '{') {
                    out.kind = JsonValue::Kind::Object;
                    ++m_I;
                    Space();
                    if (m_I < m_S.size() && m_S[m_I] == '}') { ++m_I; return true; }
                    while (true) {
                        Space();
                        std::string key;
                        if (!String(key)) return false;
                        Space();
                        if (m_I >= m_S.size() || m_S[m_I] != ':') return false;
                        ++m_I;
                        JsonValue v;
                        if (!Value(v, depth + 1)) return false;
                        out.members[key] = std::move(v);
                        Space();
                        if (m_I < m_S.size() && m_S[m_I] == ',') { ++m_I; continue; }
                        if (m_I < m_S.size() && m_S[m_I] == '}') { ++m_I; return true; }
                        return false;
                    }
                }
                if (c == '[') {
                    out.kind = JsonValue::Kind::Array;
                    ++m_I;
                    Space();
                    if (m_I < m_S.size() && m_S[m_I] == ']') { ++m_I; return true; }
                    while (true) {
                        JsonValue v;
                        if (!Value(v, depth + 1)) return false;
                        Space();
                        if (m_I < m_S.size() && m_S[m_I] == ',') { ++m_I; continue; }
                        if (m_I < m_S.size() && m_S[m_I] == ']') { ++m_I; return true; }
                        return false;
                    }
                }
                if (c == '"') { out.kind = JsonValue::Kind::String; return String(out.text); }
                if (Literal("true")) { out.kind = JsonValue::Kind::Bool; out.text = "true"; return true; }
                if (Literal("false")) { out.kind = JsonValue::Kind::Bool; out.text = "false"; return true; }
                if (Literal("null")) { out.kind = JsonValue::Kind::Null; return true; }
                const std::size_t start = m_I;
                if (m_I < m_S.size() && m_S[m_I] == '-') ++m_I;
                bool digits = false;
                while (m_I < m_S.size() && (std::isdigit(static_cast<unsigned char>(m_S[m_I])) || m_S[m_I] == '.' || m_S[m_I] == 'e' ||
                                            m_S[m_I] == 'E' || m_S[m_I] == '+' || m_S[m_I] == '-')) {
                    digits |= std::isdigit(static_cast<unsigned char>(m_S[m_I])) != 0;
                    ++m_I;
                }
                if (!digits) return false;
                out.kind = JsonValue::Kind::Number;
                out.text = m_S.substr(start, m_I - start);
                return true;
            }
            const std::string& m_S;
            std::size_t m_I = 0;
        };

        // JSONObject.getString: strings, and other scalars as their text.
        std::optional<std::string> ScalarText(const JsonValue& v) {
            if (v.kind == JsonValue::Kind::String || v.kind == JsonValue::Kind::Number || v.kind == JsonValue::Kind::Bool) return v.text;
            return std::nullopt;
        }

        template<typename T, typename Ex>
        rt::Task<T> Call(kiln::Function<int(kiln::Function<void(CredentialManagerHost::Outcome)>)> start, const char* unsupportedType) {
            if (!start) throw Ex(unsupportedType, kUnsupported);
            auto result = std::make_shared<rt::Channel<CredentialManagerHost::Outcome>>(1);
            struct Pending {
                int id = -1;
                ~Pending() { if (id >= 0 && Host().cancel) Host().cancel(id); }
            } pending;
            pending.id = start([result](CredentialManagerHost::Outcome o) { result->TrySend(std::move(o)); });
            auto got = co_await result->Next();
            pending.id = -1;
            if (!got) throw Ex(unsupportedType, kUnsupported);
            if (!got->ok) throw Ex(got->type, got->message);
            co_return T{ std::move(got->type), std::move(got->data) };
        }
    }

    Bundle GetCredentialRequest::MetadataBundle() const {
        Bundle b;
        b.Put(credentials::kBundleKeyPreferIdentityDocUi, preferIdentityDocUi);
        b.Put(credentials::kBundleKeyPreferImmediatelyAvailableCredentials, preferImmediatelyAvailableCredentials);
        return b;
    }

    Bundle CreateCredentialDisplayInfo::ToBundle() const {
        Bundle b;
        b.Put(credentials::kBundleKeyUserId, userId);
        if (userDisplayName && !userDisplayName->empty()) b.Put(credentials::kBundleKeyUserDisplayName, *userDisplayName);
        if (preferDefaultProvider && !preferDefaultProvider->empty()) b.Put(credentials::kBundleKeyDefaultProvider, *preferDefaultProvider);
        return b;
    }

    Bundle CreateCredentialRequest::FinalCredentialData() const {
        Bundle b = credentialData;
        b.Put(credentials::kBundleKeyRequestDisplayInfo, BundleNested{ std::make_shared<const Bundle>(displayInfo.ToBundle()) });
        return b;
    }

    CreateCredentialRequest MakeCreateCredentialRequest(std::string type, Bundle credentialData, Bundle candidateQueryData,
                                                        bool isSystemProviderRequired, bool isAutoSelectAllowed,
                                                        CreateCredentialDisplayInfo displayInfo, std::optional<std::string> origin,
                                                        bool preferImmediatelyAvailableCredentials) {
        if (displayInfo.userId.empty()) throw std::invalid_argument("userId should not be empty");
        CreateCredentialRequest r;
        r.type = std::move(type);
        r.credentialData = std::move(credentialData);
        r.candidateQueryData = std::move(candidateQueryData);
        r.isSystemProviderRequired = isSystemProviderRequired;
        r.isAutoSelectAllowed = isAutoSelectAllowed;
        r.displayInfo = std::move(displayInfo);
        r.origin = std::move(origin);
        r.preferImmediatelyAvailableCredentials = preferImmediatelyAvailableCredentials;
        r.credentialData.Put(credentials::kBundleKeyIsAutoSelectAllowed, isAutoSelectAllowed);
        r.credentialData.Put(credentials::kBundleKeyPreferImmediatelyAvailableCredentials, preferImmediatelyAvailableCredentials);
        r.candidateQueryData.Put(credentials::kBundleKeyIsAutoSelectAllowed, isAutoSelectAllowed);
        return r;
    }

    CreateCredentialRequest CreatePasswordRequest(const std::string& id, const std::string& password, std::optional<std::string> origin,
                                                  bool preferImmediatelyAvailableCredentials, bool isAutoSelectAllowed,
                                                  std::optional<std::string> preferDefaultProvider) {
        Bundle data;
        data.Put(credentials::kBundleKeyId, id);
        data.Put(credentials::kBundleKeyPassword, password);
        auto r = MakeCreateCredentialRequest(credentials::kTypePasswordCredential, std::move(data), Bundle{}, false, isAutoSelectAllowed,
                                             { id, std::nullopt, std::move(preferDefaultProvider) }, std::move(origin),
                                             preferImmediatelyAvailableCredentials);
        if (password.empty()) throw std::invalid_argument("password should not be empty");
        return r;
    }

    CreateCredentialRequest CreatePublicKeyCredentialRequest(const std::string& requestJson, std::optional<std::vector<std::uint8_t>> clientDataHash,
                                                             bool preferImmediatelyAvailableCredentials, std::optional<std::string> origin,
                                                             bool isAutoSelectAllowed, bool isConditional,
                                                             std::optional<std::string> preferDefaultProvider) {
        // getRequestDisplayInfo runs first (as the constructor's argument), then the JSON check.
        JsonValue root;
        const bool parsed = JsonReader(requestJson).Parse(root);
        CreateCredentialDisplayInfo info;
        {
            const JsonValue* user = nullptr;
            if (parsed && root.kind == JsonValue::Kind::Object)
                if (auto it = root.members.find("user"); it != root.members.end() && it->second.kind == JsonValue::Kind::Object) user = &it->second;
            std::optional<std::string> name;
            if (user)
                if (auto it = user->members.find("name"); it != user->members.end()) name = ScalarText(it->second);
            if (!name) throw std::invalid_argument("user.name must be defined in requestJson");
            info.userId = *name;
            if (auto it = user->members.find("displayName"); it != user->members.end() && it->second.kind != JsonValue::Kind::Null) {
                info.userDisplayName = ScalarText(it->second);
                if (!info.userDisplayName) throw std::invalid_argument("user.name must be defined in requestJson");
            }
            info.preferDefaultProvider = std::move(preferDefaultProvider);
        }
        Bundle data, query;
        for (Bundle* b : { &data, &query }) {
            b->Put(credentials::kBundleKeySubtype, std::string(credentials::kSubtypeCreatePublicKeyCredentialRequest));
            b->Put(credentials::kBundleKeyRequestJson, requestJson);
            b->Put(credentials::kBundleKeyClientDataHash, clientDataHash);
        }
        auto r = MakeCreateCredentialRequest(credentials::kTypePublicKeyCredential, std::move(data), std::move(query), false, isAutoSelectAllowed,
                                             std::move(info), std::move(origin), preferImmediatelyAvailableCredentials);
        if (requestJson.empty() || !parsed || (root.kind != JsonValue::Kind::Object && root.kind != JsonValue::Kind::Array))
            throw std::invalid_argument("requestJson must not be empty, and must be a valid JSON");
        if (isConditional) {
            r.candidateQueryData.Put(credentials::kBundleKeyIsConditionalRequest, true);
            r.credentialData.Put(credentials::kBundleKeyIsConditionalRequest, true);
        }
        return r;
    }

    rt::Task<Credential> GetCredential(GetCredentialRequest request) {
        kiln::Function<int(kiln::Function<void(CredentialManagerHost::Outcome)>)> start;
        if (Host().get) start = [request = std::move(request)](kiln::Function<void(CredentialManagerHost::Outcome)> done) { return Host().get(request, std::move(done)); };
        co_return co_await Call<Credential, GetCredentialException>(std::move(start), credentials::kGetTypeUnsupported);
    }

    rt::Task<CreateCredentialResponse> CreateCredential(CreateCredentialRequest request) {
        kiln::Function<int(kiln::Function<void(CredentialManagerHost::Outcome)>)> start;
        if (Host().create) start = [request = std::move(request)](kiln::Function<void(CredentialManagerHost::Outcome)> done) { return Host().create(request, std::move(done)); };
        co_return co_await Call<CreateCredentialResponse, CreateCredentialException>(std::move(start), credentials::kCreateTypeUnsupported);
    }

    rt::Task<> ClearCredentialState() {
        kiln::Function<int(kiln::Function<void(CredentialManagerHost::Outcome)>)> start;
        if (Host().clear) start = [](kiln::Function<void(CredentialManagerHost::Outcome)> done) { return Host().clear(std::move(done)); };
        struct Empty {
            Empty(std::string, Bundle) {}
        };
        co_await Call<Empty, ClearCredentialException>(std::move(start), credentials::kClearTypeUnsupported);
    }

    void SetCredentialManagerHost(CredentialManagerHost host) { Host() = std::move(host); }

}
