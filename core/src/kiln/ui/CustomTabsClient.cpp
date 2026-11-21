#include "kiln/ui/CustomTabsClient.h"

#include "kiln/runtime/Task.h"

#include <map>
#include <mutex>
#include <stdexcept>

namespace kiln::ui {

    namespace {
        struct State {
            std::mutex mutex;
            CustomTabsHost host;
            struct Binding {
                std::string packageName;
                std::shared_ptr<CustomTabsServiceConnection> connection;
            };
            std::map<int, Binding> bindings;
            std::map<int, std::shared_ptr<CustomTabsCallback>> callbacks;
            std::map<int, std::shared_ptr<EngagementSignalsCallback>> engagement;
            int nextEngagement = 0;
            // The dispatcher of the thread that bound (the main one): Dispatcher::Main() is per
            // thread, and callbacks arrive on binder threads.
            rt::Dispatcher* home = nullptr;
        };
        State& S() {
            static State s;
            return s;
        }
        CustomTabsHost& Host() { return S().host; }
        void PostHome(kiln::Function<void()> fn) {
            rt::Dispatcher* home;
            {
                std::lock_guard lock(S().mutex);
                home = S().home;
            }
            if (home) home->PostFromAnyThread(std::move(fn));
        }
        const Bundle& Extras(const Bundle& args) {
            static const Bundle empty;
            const Bundle* b = args.GetBundle("extras");
            return b ? *b : empty;
        }
        std::int32_t Int(const Bundle& b, const char* key) {
            const BundleValue* v = b.Get(key);
            const auto* i = v ? std::get_if<std::int32_t>(v) : nullptr;
            return i ? *i : 0;
        }
        bool Bool(const Bundle& b, const char* key) {
            const BundleValue* v = b.Get(key);
            const auto* i = v ? std::get_if<bool>(v) : nullptr;
            return i && *i;
        }
        std::string Text(const Bundle& b, const char* key) {
            const std::string* s = b.GetString(key);
            return s ? *s : std::string();
        }
        BundleNested Nested(const Bundle& b) { return BundleNested{ std::make_shared<Bundle>(b) }; }
    }

    void SetCustomTabsHost(CustomTabsHost host) {
        std::lock_guard lock(S().mutex);
        S().host = std::move(host);
    }

    int BindCustomTabsService(const std::string& packageName, CustomTabsServiceConnection connection, bool preservePriority) {
        if (packageName.empty()) throw std::invalid_argument("Service Intents must be explicit");
        if (!Host().bind) return 0;
        const int id = Host().bind(packageName, preservePriority);
        if (id == 0) return 0;
        std::lock_guard lock(S().mutex);
        S().home = &rt::Dispatcher::Main();
        S().bindings[id] = { packageName, std::make_shared<CustomTabsServiceConnection>(std::move(connection)) };
        return id;
    }

    void UnbindCustomTabsService(int binding) {
        {
            std::lock_guard lock(S().mutex);
            S().bindings.erase(binding);
        }
        if (Host().unbind) Host().unbind(binding);
    }

    std::optional<std::string> GetCustomTabsPackageName(const std::vector<std::string>& packages, bool ignoreDefault) {
        return Host().packageName ? Host().packageName(packages, ignoreDefault) : std::nullopt;
    }

    bool ConnectAndInitialize(const std::string& packageName) {
        // The binding is kept, as upstream's (it connects with the application context).
        return BindCustomTabsService(packageName, { .onConnected = [](std::shared_ptr<CustomTabsClient> client) { client->Warmup(0); } }) != 0;
    }

    namespace {
        bool HasCategory(const std::string& provider, const char* category) {
            return Host().packageHasCategory && Host().packageHasCategory(provider, category);
        }
    }
    bool IsEphemeralBrowsingSupported(const std::string& p) { return HasCategory(p, "androidx.browser.customtabs.category.EphemeralBrowsing"); }
    bool IsSetNetworkSupported(const std::string& p) { return HasCategory(p, "androidx.browser.customtabs.category.SetNetwork"); }
    bool IsAuthTabSupported(const std::string& p) { return HasCategory(p, "androidx.browser.auth.category.AuthTab"); }

    void DispatchCustomTabsConnection(int binding, bool connected) {
        PostHome([binding, connected] {
            std::shared_ptr<CustomTabsServiceConnection> c;
            std::string package;
            {
                std::lock_guard lock(S().mutex);
                auto it = S().bindings.find(binding);
                if (it == S().bindings.end()) return;
                c = it->second.connection;
                package = it->second.packageName;
            }
            if (connected) {
                if (c->onConnected) c->onConnected(std::make_shared<CustomTabsClient>(binding, package));
            } else if (c->onDisconnected) {
                c->onDisconnected();
            }
        });
    }

    bool CustomTabsClient::Warmup(std::int64_t flags) { return Host().warmup && Host().warmup(m_Connection, flags); }

    std::shared_ptr<CustomTabsSession> CustomTabsClient::NewSession(CustomTabsCallback callback, std::optional<int> id) {
        if (!Host().newSession) return nullptr;
        const int session = Host().newSession(m_Connection, id);
        if (session == 0) return nullptr;
        {
            std::lock_guard lock(S().mutex);
            S().home = &rt::Dispatcher::Main();
            S().callbacks[session] = std::make_shared<CustomTabsCallback>(std::move(callback));
        }
        return std::make_shared<CustomTabsSession>(session, m_Package, id);
    }

    std::shared_ptr<AuthTabSession> CustomTabsClient::NewAuthTabSession(AuthTabCallback callback, std::optional<int> id) {
        if (!Host().newAuthTabSession) return nullptr;
        const int session = Host().newAuthTabSession(m_Connection, id);
        if (session == 0) return nullptr;
        // The host reports its events under a Custom Tabs session's names.
        auto shared = std::make_shared<AuthTabCallback>(std::move(callback));
        CustomTabsCallback routed;
        routed.onNavigationEvent = [shared](int e, const Bundle& b) { if (shared->onNavigationEvent) shared->onNavigationEvent(e, b); };
        routed.extraCallback = [shared](const std::string& n, const Bundle& b) { if (shared->onExtraCallback) shared->onExtraCallback(n, b); };
        routed.extraCallbackWithResult = [shared](const std::string& n, const Bundle& b) -> std::optional<Bundle> {
            return shared->onExtraCallbackWithResult ? shared->onExtraCallbackWithResult(n, b) : std::nullopt;
        };
        routed.onWarmupCompleted = [shared](const Bundle& b) { if (shared->onWarmupCompleted) shared->onWarmupCompleted(b); };
        {
            std::lock_guard lock(S().mutex);
            S().home = &rt::Dispatcher::Main();
            S().callbacks[session] = std::make_shared<CustomTabsCallback>(std::move(routed));
        }
        return std::make_shared<AuthTabSession>(session, m_Package, id);
    }

    std::optional<Bundle> CustomTabsClient::ExtraCommand(const std::string& commandName, const Bundle& args) {
        return Host().extraCommand ? Host().extraCommand(m_Connection, commandName, args) : std::nullopt;
    }

    CustomTabsSession::~CustomTabsSession() {
        // The callback stays registered while the browser may still call it; a new session id
        // never reuses it.
    }

    Bundle CustomTabsSession::Call(const std::string& method, Bundle args) {
        return Host().sessionCall ? Host().sessionCall(m_Id, method, args) : Bundle{};
    }

    bool CustomTabsSession::MayLaunchUrl(std::optional<std::string> url, const Bundle& extras, const std::vector<Bundle>& otherLikelyBundles) {
        Bundle a;
        if (url) a.Put("url", *url);
        a.Put("extras", Nested(extras));
        BundleList others;
        for (const auto& b : otherLikelyBundles) others.bundles.push_back(std::make_shared<Bundle>(b));
        if (!others.bundles.empty()) a.Put("others", std::move(others));
        return Int(Call("mayLaunchUrl", std::move(a)), "result") == 1;
    }

    namespace {
        // PrefetchOptions.toBundle
        Bundle PrefetchBundle(const PrefetchOptions& o) {
            Bundle b;
            b.Put("androidx.browser.customtabs.PrefetchOptions.KEY_REQUIRES_ANONYMOUS_IP_WHEN_CROSS_ORIGIN", o.requiresAnonymousIpWhenCrossOrigin);
            if (o.sourceOrigin) b.Put("androidx.browser.customtabs.PrefetchOptions.KEY_SOURCE_ORIGIN", BundleUri{ *o.sourceOrigin });
            return b;
        }
    }

    void CustomTabsSession::Prefetch(const std::string& url, const PrefetchOptions& options) {
        Bundle a;
        a.Put("url", url);
        a.Put("options", Nested(PrefetchBundle(options)));
        Call("prefetch", std::move(a));
    }

    void CustomTabsSession::Prefetch(const std::vector<std::string>& urls, const PrefetchOptions& options) {
        Bundle a;
        a.Put("urls", urls);
        a.Put("options", Nested(PrefetchBundle(options)));
        Call("prefetch", std::move(a));
    }

    namespace {
        constexpr const char* kKeyId = "android.support.customtabs.customaction.ID";
        constexpr const char* kKeyIcon = "android.support.customtabs.customaction.ICON";
        constexpr const char* kKeyDescription = "android.support.customtabs.customaction.DESCRIPTION";
        constexpr const char* kExtraActionButtonBundle = "android.support.customtabs.extra.ACTION_BUTTON_BUNDLE";
        constexpr const char* kExtraSwipeUpGesture = "androidx.browser.customtabs.extra.SECONDARY_TOOLBAR_SWIPE_UP_GESTURE";
    }

    bool CustomTabsSession::SetActionButton(const CustomTabIcon& icon, const std::string& description) {
        Bundle button;
        button.Put(kKeyIcon, BundleBitmap{ icon.encoded, icon.drawable });
        button.Put(kKeyDescription, description);
        // setActionButton adds the session id to the button's bundle, not the outer one.
        if (m_SessionId) button.Put("android.support.customtabs.extra.SESSION_ID", BundleCustomTabsSessionId{ *m_SessionId });
        Bundle meta;
        meta.Put(kExtraActionButtonBundle, Nested(button));
        Bundle a;
        a.Put("bundle", Nested(meta));
        a.Put("idInBundle", false);
        return Int(Call("updateVisuals", std::move(a)), "result") == 1;
    }

    bool CustomTabsSession::SetToolbarItem(int id, const CustomTabIcon& icon, const std::string& description) {
        Bundle button;
        button.Put(kKeyId, std::int32_t(id));
        button.Put(kKeyIcon, BundleBitmap{ icon.encoded, icon.drawable });
        button.Put(kKeyDescription, description);
        Bundle meta;
        meta.Put(kExtraActionButtonBundle, Nested(button));
        Bundle a;
        a.Put("bundle", Nested(meta));
        a.Put("idInBundle", true);
        return Int(Call("updateVisuals", std::move(a)), "result") == 1;
    }

    bool CustomTabsSession::SetSecondaryToolbarSwipeUpGesture(std::optional<std::string> deepLink) {
        Bundle b;
        if (deepLink) b.Put(kExtraSwipeUpGesture, BundlePendingIntent{ *deepLink });
        Bundle a;
        a.Put("bundle", Nested(b));
        a.Put("idInBundle", true);
        return Int(Call("updateVisuals", std::move(a)), "result") == 1;
    }

    bool CustomTabsSession::RequestPostMessageChannel(const std::string& origin, std::optional<std::string> targetOrigin, const Bundle& extras) {
        Bundle a;
        a.Put("origin", origin);
        if (targetOrigin) a.Put("targetOrigin", *targetOrigin);
        a.Put("extras", Nested(extras));
        return Int(Call("requestPostMessageChannel", std::move(a)), "result") == 1;
    }

    int CustomTabsSession::PostMessage(const std::string& message, const Bundle& extras) {
        Bundle a;
        a.Put("message", message);
        a.Put("extras", Nested(extras));
        const Bundle r = Call("postMessage", std::move(a));
        return r.Get("result") ? Int(r, "result") : customtabs::kResultFailureRemoteError;
    }

    bool CustomTabsSession::ValidateRelationship(int relation, const std::string& origin, const Bundle& extras) {
        if (relation < customtabs::kRelationUseAsOrigin || relation > customtabs::kRelationHandleAllUrls) return false;
        Bundle a;
        a.Put("relation", std::int32_t(relation));
        a.Put("origin", origin);
        a.Put("extras", Nested(extras));
        return Int(Call("validateRelationship", std::move(a)), "result") == 1;
    }

    bool CustomTabsSession::ReceiveFile(const std::string& uri, int purpose, const Bundle& extras) {
        Bundle a;
        a.Put("uri", uri);
        a.Put("purpose", std::int32_t(purpose));
        a.Put("extras", Nested(extras));
        return Int(Call("receiveFile", std::move(a)), "result") == 1;
    }

    bool CustomTabsSession::IsEngagementSignalsApiAvailable(const Bundle& extras) {
        Bundle a;
        a.Put("extras", Nested(extras));
        const Bundle r = Call("isEngagementSignalsApiAvailable", std::move(a));
        if (Bool(r, "unsupported")) throw std::logic_error("This method isn't supported by the Custom Tabs implementation.");
        return Int(r, "result") == 1;
    }

    bool CustomTabsSession::SetEngagementSignalsCallback(EngagementSignalsCallback callback, const Bundle& extras) {
        int id;
        {
            std::lock_guard lock(S().mutex);
            id = ++S().nextEngagement;
            S().engagement[id] = std::make_shared<EngagementSignalsCallback>(std::move(callback));
        }
        Bundle a;
        a.Put("engagement", std::int32_t(id));
        a.Put("extras", Nested(extras));
        const Bundle r = Call("setEngagementSignalsCallback", std::move(a));
        if (Bool(r, "unsupported")) throw std::logic_error("This method isn't supported by the Custom Tabs implementation.");
        return Int(r, "result") == 1;
    }

    void DispatchCustomTabsCallback(int session, const std::string& name, const Bundle& args) {
        PostHome([session, name, args] {
            std::shared_ptr<CustomTabsCallback> cb;
            {
                std::lock_guard lock(S().mutex);
                auto it = S().callbacks.find(session);
                if (it == S().callbacks.end()) return;
                cb = it->second;
            }
            const Bundle& extras = Extras(args);
            if (name == "onNavigationEvent") { if (cb->onNavigationEvent) cb->onNavigationEvent(Int(args, "event"), extras); }
            else if (name == "extraCallback") { if (cb->extraCallback) cb->extraCallback(Text(args, "name"), extras); }
            else if (name == "onMessageChannelReady") { if (cb->onMessageChannelReady) cb->onMessageChannelReady(extras); }
            else if (name == "onPostMessage") { if (cb->onPostMessage) cb->onPostMessage(Text(args, "message"), extras); }
            else if (name == "onRelationshipValidationResult") {
                if (cb->onRelationshipValidationResult) cb->onRelationshipValidationResult(Int(args, "relation"), Text(args, "origin"), Bool(args, "result"), extras);
            }
            else if (name == "onActivityResized") { if (cb->onActivityResized) cb->onActivityResized(Int(args, "height"), Int(args, "width"), extras); }
            else if (name == "onWarmupCompleted") { if (cb->onWarmupCompleted) cb->onWarmupCompleted(extras); }
            else if (name == "onActivityLayout") {
                if (cb->onActivityLayout) cb->onActivityLayout(Int(args, "left"), Int(args, "top"), Int(args, "right"), Int(args, "bottom"), Int(args, "state"), extras);
            }
            else if (name == "onMinimized") { if (cb->onMinimized) cb->onMinimized(extras); }
            else if (name == "onUnminimized") { if (cb->onUnminimized) cb->onUnminimized(extras); }
        });
    }

    std::optional<Bundle> DispatchCustomTabsCallbackWithResult(int session, const std::string& name, const Bundle& args) {
        std::shared_ptr<CustomTabsCallback> cb;
        {
            std::lock_guard lock(S().mutex);
            auto it = S().callbacks.find(session);
            if (it == S().callbacks.end()) return std::nullopt;
            cb = it->second;
        }
        return cb->extraCallbackWithResult ? cb->extraCallbackWithResult(name, Extras(args)) : std::nullopt;
    }

    void DispatchEngagementSignal(int callback, const std::string& name, const Bundle& args) {
        PostHome([callback, name, args] {
            std::shared_ptr<EngagementSignalsCallback> cb;
            {
                std::lock_guard lock(S().mutex);
                auto it = S().engagement.find(callback);
                if (it == S().engagement.end()) return;
                cb = it->second;
            }
            const Bundle& extras = Extras(args);
            if (name == "onVerticalScrollEvent") { if (cb->onVerticalScrollEvent) cb->onVerticalScrollEvent(Bool(args, "value"), extras); }
            else if (name == "onGreatestScrollPercentageIncreased") { if (cb->onGreatestScrollPercentageIncreased) cb->onGreatestScrollPercentageIncreased(Int(args, "value"), extras); }
            else if (name == "onSessionEnded") { if (cb->onSessionEnded) cb->onSessionEnded(Bool(args, "value"), extras); }
        });
    }

}
