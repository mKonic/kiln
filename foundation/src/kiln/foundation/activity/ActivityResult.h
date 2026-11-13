#pragma once

// rememberLauncherForActivityResult (activity-compose) and ActivityResultContracts: a launcher
// that starts what a contract describes (a document picker, the camera, a permission request)
// and hands the parsed result to onResult, also after the activity was recreated meanwhile.

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/ActivityResult.h"
#include "kiln/ui/Locals.h"

#include <cstdint>
#include <map>
#include <memory>
#include <algorithm>
#include <climits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace kiln::foundation {

    using ui::ActivityResult;
    using ui::Intent;

    template<typename I, typename O>
    class ActivityResultContract {
    public:
        virtual ~ActivityResultContract() = default;
        virtual Intent CreateIntent(const I& input) const = 0;
        virtual O ParseResult(int resultCode, const std::optional<Intent>& intent) const = 0;
        // getSynchronousResult: an answer without starting anything (permissions already held).
        virtual std::optional<O> GetSynchronousResult(const ui::ActivityResultHost&, const I&) const { return std::nullopt; }
    };

    namespace intents {
        inline constexpr const char* kActionGetContent = "android.intent.action.GET_CONTENT";
        inline constexpr const char* kActionOpenDocument = "android.intent.action.OPEN_DOCUMENT";
        inline constexpr const char* kActionOpenDocumentTree = "android.intent.action.OPEN_DOCUMENT_TREE";
        inline constexpr const char* kActionCreateDocument = "android.intent.action.CREATE_DOCUMENT";
        inline constexpr const char* kActionPick = "android.intent.action.PICK";
        inline constexpr const char* kActionImageCapture = "android.media.action.IMAGE_CAPTURE";
        inline constexpr const char* kActionVideoCapture = "android.media.action.VIDEO_CAPTURE";
        inline constexpr const char* kCategoryOpenable = "android.intent.category.OPENABLE";
        inline constexpr const char* kExtraAllowMultiple = "android.intent.extra.ALLOW_MULTIPLE";
        inline constexpr const char* kExtraMimeTypes = "android.intent.extra.MIME_TYPES";
        inline constexpr const char* kExtraTitle = "android.intent.extra.TITLE";
        inline constexpr const char* kExtraOutput = "output";
        inline constexpr const char* kExtraInitialUri = "android.provider.extra.INITIAL_URI";
        inline constexpr const char* kContactsContentType = "vnd.android.cursor.dir/contact";
        inline constexpr int kFlagGrantReadUriPermission = 1, kFlagGrantWriteUriPermission = 2;
        // RequestMultiplePermissions.
        inline constexpr const char* kActionRequestPermissions = "androidx.activity.result.contract.action.REQUEST_PERMISSIONS";
        inline constexpr const char* kExtraPermissions = "androidx.activity.result.contract.extra.PERMISSIONS";
        inline constexpr const char* kExtraPermissionGrantResults = "androidx.activity.result.contract.extra.PERMISSION_GRANT_RESULTS";
        // PickVisualMedia's request, resolved by the host into the system picker, the system
        // fallback picker or ACTION_OPEN_DOCUMENT (it needs the device's picker availability).
        inline constexpr const char* kActionPickVisualMedia = "dev.kiln.action.PICK_VISUAL_MEDIA";
        inline constexpr int kPermissionGranted = 0;

        // Intent.getClipDataUris: the data URI, then the clip's, without repeats.
        inline std::vector<std::string> ClipDataUris(const Intent& i) {
            std::vector<std::string> out;
            auto add = [&](const std::string& u) { if (std::find(out.begin(), out.end(), u) == out.end()) out.push_back(u); };
            if (i.data) add(*i.data);
            for (const auto& u : i.clipUris) add(u);
            return out;
        }
    }

    namespace contracts {
        using UriResult = std::optional<std::string>;

        struct StartActivityForResult final : ActivityResultContract<Intent, ActivityResult> {
            Intent CreateIntent(const Intent& input) const override { return input; }
            ActivityResult ParseResult(int code, const std::optional<Intent>& intent) const override { return { code, intent }; }
        };

        struct RequestMultiplePermissions final : ActivityResultContract<std::vector<std::string>, std::map<std::string, bool>> {
            static Intent Create(const std::vector<std::string>& input) {
                Intent i;
                i.action = intents::kActionRequestPermissions;
                i.extras.Put(intents::kExtraPermissions, input);
                return i;
            }
            Intent CreateIntent(const std::vector<std::string>& input) const override { return Create(input); }
            std::optional<std::map<std::string, bool>> GetSynchronousResult(const ui::ActivityResultHost& host,
                                                                            const std::vector<std::string>& input) const override {
                if (input.empty()) return std::map<std::string, bool>{};
                for (const auto& p : input)
                    if (!host.checkSelfPermission || !host.checkSelfPermission(p)) return std::nullopt;
                std::map<std::string, bool> all;
                for (const auto& p : input) all[p] = true;
                return all;
            }
            std::map<std::string, bool> ParseResult(int code, const std::optional<Intent>& intent) const override {
                std::map<std::string, bool> out;
                if (code != ui::kResultOk || !intent) return out;
                const auto* perms = intent->extras.Get(intents::kExtraPermissions);
                const auto* grants = intent->extras.Get(intents::kExtraPermissionGrantResults);
                const auto* p = perms ? std::get_if<std::vector<std::string>>(perms) : nullptr;
                const auto* g = grants ? std::get_if<std::vector<std::int32_t>>(grants) : nullptr;
                if (!p || !g) return out;
                for (std::size_t k = 0; k < p->size() && k < g->size(); ++k) out[(*p)[k]] = (*g)[k] == intents::kPermissionGranted;
                return out;
            }
        };

        struct RequestPermission final : ActivityResultContract<std::string, bool> {
            Intent CreateIntent(const std::string& input) const override { return RequestMultiplePermissions::Create({ input }); }
            bool ParseResult(int code, const std::optional<Intent>& intent) const override {
                if (!intent || code != ui::kResultOk) return false;
                const auto* grants = intent->extras.Get(intents::kExtraPermissionGrantResults);
                const auto* g = grants ? std::get_if<std::vector<std::int32_t>>(grants) : nullptr;
                return g && std::any_of(g->begin(), g->end(), [](std::int32_t r) { return r == intents::kPermissionGranted; });
            }
            std::optional<bool> GetSynchronousResult(const ui::ActivityResultHost& host, const std::string& input) const override {
                if (host.checkSelfPermission && host.checkSelfPermission(input)) return true;
                return std::nullopt;
            }
        };

        // TakePicture / CaptureVideo: into the given content URI; true when the app reported success.
        struct TakePicture final : ActivityResultContract<std::string, bool> {
            Intent CreateIntent(const std::string& uri) const override {
                Intent i;
                i.action = intents::kActionImageCapture;
                i.extras.Put(intents::kExtraOutput, ui::BundleUri{ uri });
                i.flags = intents::kFlagGrantReadUriPermission | intents::kFlagGrantWriteUriPermission;
                return i;
            }
            bool ParseResult(int code, const std::optional<Intent>&) const override { return code == ui::kResultOk; }
        };
        struct CaptureVideo final : ActivityResultContract<std::string, bool> {
            Intent CreateIntent(const std::string& uri) const override {
                Intent i;
                i.action = intents::kActionVideoCapture;
                i.extras.Put(intents::kExtraOutput, ui::BundleUri{ uri });
                i.flags = intents::kFlagGrantReadUriPermission | intents::kFlagGrantWriteUriPermission;
                return i;
            }
            bool ParseResult(int code, const std::optional<Intent>&) const override { return code == ui::kResultOk; }
        };

        inline UriResult DataIfOk(int code, const std::optional<Intent>& intent) {
            return code == ui::kResultOk && intent ? intent->data : std::nullopt;
        }
        inline std::vector<std::string> ClipUrisIfOk(int code, const std::optional<Intent>& intent) {
            return code == ui::kResultOk && intent ? intents::ClipDataUris(*intent) : std::vector<std::string>{};
        }

        struct PickContact final : ActivityResultContract<std::monostate, UriResult> {
            Intent CreateIntent(const std::monostate&) const override {
                Intent i;
                i.action = intents::kActionPick;
                i.type = intents::kContactsContentType;
                return i;
            }
            UriResult ParseResult(int code, const std::optional<Intent>& intent) const override { return DataIfOk(code, intent); }
        };

        // GetContent: the MIME type to pick.
        struct GetContent final : ActivityResultContract<std::string, UriResult> {
            Intent CreateIntent(const std::string& type) const override {
                Intent i;
                i.action = intents::kActionGetContent;
                i.categories = { intents::kCategoryOpenable };
                i.type = type;
                return i;
            }
            UriResult ParseResult(int code, const std::optional<Intent>& intent) const override { return DataIfOk(code, intent); }
        };
        struct GetMultipleContents final : ActivityResultContract<std::string, std::vector<std::string>> {
            Intent CreateIntent(const std::string& type) const override {
                Intent i = GetContent{}.CreateIntent(type);
                i.extras.Put(intents::kExtraAllowMultiple, true);
                return i;
            }
            std::vector<std::string> ParseResult(int code, const std::optional<Intent>& intent) const override { return ClipUrisIfOk(code, intent); }
        };

        // OpenDocument: the MIME types to offer.
        struct OpenDocument final : ActivityResultContract<std::vector<std::string>, UriResult> {
            Intent CreateIntent(const std::vector<std::string>& types) const override {
                Intent i;
                i.action = intents::kActionOpenDocument;
                i.extras.Put(intents::kExtraMimeTypes, types);
                i.type = "*/*";
                return i;
            }
            UriResult ParseResult(int code, const std::optional<Intent>& intent) const override { return DataIfOk(code, intent); }
        };
        struct OpenMultipleDocuments final : ActivityResultContract<std::vector<std::string>, std::vector<std::string>> {
            Intent CreateIntent(const std::vector<std::string>& types) const override {
                Intent i = OpenDocument{}.CreateIntent(types);
                i.extras.Put(intents::kExtraAllowMultiple, true);
                return i;
            }
            std::vector<std::string> ParseResult(int code, const std::optional<Intent>& intent) const override { return ClipUrisIfOk(code, intent); }
        };
        // OpenDocumentTree: the folder to start in, if any.
        struct OpenDocumentTree final : ActivityResultContract<UriResult, UriResult> {
            Intent CreateIntent(const UriResult& initial) const override {
                Intent i;
                i.action = intents::kActionOpenDocumentTree;
                if (initial) i.extras.Put(intents::kExtraInitialUri, ui::BundleUri{ *initial });
                return i;
            }
            UriResult ParseResult(int code, const std::optional<Intent>& intent) const override { return DataIfOk(code, intent); }
        };
        // CreateDocument(mimeType): the suggested file name in, the created document out.
        struct CreateDocument final : ActivityResultContract<std::string, UriResult> {
            explicit CreateDocument(std::string mimeType) : mimeType(std::move(mimeType)) {}
            std::string mimeType;
            Intent CreateIntent(const std::string& title) const override {
                Intent i;
                i.action = intents::kActionCreateDocument;
                i.type = mimeType;
                i.extras.Put(intents::kExtraTitle, title);
                return i;
            }
            UriResult ParseResult(int code, const std::optional<Intent>& intent) const override { return DataIfOk(code, intent); }
        };

        // PickVisualMediaRequest. mediaType: "image/*" (ImageOnly), "video/*" (VideoOnly), a
        // single MIME type, or none (ImageAndVideo).
        struct PickVisualMediaRequest {
            std::optional<std::string> mediaType;
            int maxItems = 0;                       // 0: PickMultipleVisualMedia.getMaxItems()
            bool isOrderedSelection = false;
            int defaultTab = 1;                     // PICK_IMAGES_TAB_IMAGES (PhotosTab); 0 albums
            std::optional<std::int64_t> accentColor;
            std::vector<std::int32_t> supportedHdrTypes;   // MediaCapabilities (API 33)
            bool transcode = false;                 // mediaCapabilitiesForTranscoding is set
        };
        inline Intent PickVisualMediaIntent(const PickVisualMediaRequest& r, std::optional<int> maxItems) {
            Intent i;
            i.action = intents::kActionPickVisualMedia;
            if (r.mediaType) i.extras.Put("type", *r.mediaType);
            if (maxItems) i.extras.Put("max", std::int32_t(*maxItems));
            i.extras.Put("tab", std::int32_t(r.defaultTab));
            i.extras.Put("ordered", r.isOrderedSelection);
            if (r.accentColor) i.extras.Put("accent", *r.accentColor);
            if (r.transcode) i.extras.Put("hdr", r.supportedHdrTypes);
            return i;
        }
        struct PickVisualMedia final : ActivityResultContract<PickVisualMediaRequest, UriResult> {
            Intent CreateIntent(const PickVisualMediaRequest& r) const override { return PickVisualMediaIntent(r, std::nullopt); }
            UriResult ParseResult(int code, const std::optional<Intent>& intent) const override {
                if (code != ui::kResultOk || !intent) return std::nullopt;
                if (intent->data) return intent->data;
                const auto uris = intents::ClipDataUris(*intent);
                return uris.empty() ? std::nullopt : UriResult(uris.front());
            }
        };
        // PickMultipleVisualMedia(maxItems): 0 for the picker's limit; otherwise more than one.
        struct PickMultipleVisualMedia final : ActivityResultContract<PickVisualMediaRequest, std::vector<std::string>> {
            explicit PickMultipleVisualMedia(int maxItems = 0) : maxItems(maxItems) {
                if (maxItems != 0 && maxItems <= 1) throw std::invalid_argument("Max items must be higher than 1");
            }
            int maxItems;
            Intent CreateIntent(const PickVisualMediaRequest& r) const override {
                const int a = maxItems ? maxItems : INT32_MAX, b = r.maxItems ? r.maxItems : INT32_MAX;
                return PickVisualMediaIntent(r, std::min(a, b));
            }
            std::vector<std::string> ParseResult(int code, const std::optional<Intent>& intent) const override { return ClipUrisIfOk(code, intent); }
        };
    }

    // ManagedActivityResultLauncher.
    template<typename I, typename O>
    class ActivityResultLauncher {
    public:
        struct Shared {
            ui::ActivityResultRegistry* registry = nullptr;
            std::string key;
            std::shared_ptr<const ActivityResultContract<I, O>> contract;
            kiln::Function<void(const O&)> onResult;
        };
        explicit ActivityResultLauncher(std::shared_ptr<Shared> s) : m_Shared(std::move(s)) {}
        // launch: a synchronous result is posted; otherwise the host starts the intent.
        void Launch(const I& input) const {
            auto s = m_Shared;
            if (!s->registry) throw std::logic_error("Launcher has not been initialized");
            if (auto sync = s->contract->GetSynchronousResult(s->registry->Host(), input)) {
                rt::Dispatcher::Main().Post([s, value = *sync] {
                    s->registry->DispatchParsed(s->key, [s, value] { if (s->onResult) s->onResult(value); }, s->registry->HasCallback(s->key));
                });
                return;
            }
            s->registry->Launch(s->key, s->contract->CreateIntent(input));
        }
        const ActivityResultContract<I, O>& Contract() const { return *m_Shared->contract; }

    private:
        std::shared_ptr<Shared> m_Shared;
    };

    using ui::LocalActivityResultRegistry;

    // rememberLauncherForActivityResult.
    template<typename I, typename O>
    ActivityResultLauncher<I, O> RememberLauncherForActivityResult(std::shared_ptr<const ActivityResultContract<I, O>> contract,
                                                                   kiln::Function<void(const O&)> onResult, rt::Key key = {}) {
        auto* c = rt::Composition::Current();
        // rememberSaveable { UUID.randomUUID().toString() }
        const std::string& launcherKey = rt::RememberSaveable<std::string>([] {
            std::random_device rd;
            std::mt19937_64 g((std::uint64_t(rd()) << 32) ^ rd());
            static const char* hex = "0123456789abcdef";
            std::string s;
            for (int i = 0; i < 32; ++i) s += hex[g() & 15];
            return s;
        }, rt::AutoSaver<std::string>(), {}, key);
        ui::ActivityResultRegistry* registry = c->Read(LocalActivityResultRegistry);
        if (!registry) throw std::logic_error("No ActivityResultRegistryOwner was provided via LocalActivityResultRegistryOwner");
        using Shared = typename ActivityResultLauncher<I, O>::Shared;
        auto shared = c->Remember<std::shared_ptr<Shared>>([] { return std::make_shared<Shared>(); }, {}, key);
        // rememberUpdatedState for the contract and callback.
        shared->onResult = std::move(onResult);
        shared->contract = contract;
        shared->registry = registry;
        shared->key = launcherKey;
        c->DisposableEffect(rt::Keys(registry, std::hash<std::string>{}(launcherKey), contract.get()), [registry, launcherKey, shared] {
            registry->Register(launcherKey, [shared](const ActivityResult& r) {
                if (shared->onResult) shared->onResult(shared->contract->ParseResult(r.resultCode, r.data));
            });
            return kiln::Function<void()>([registry, launcherKey] { registry->Unregister(launcherKey); });
        }, key);
        return ActivityResultLauncher<I, O>(shared);
    }

}
