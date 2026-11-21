#pragma once

// Custom Tabs' client (androidx.browser's CustomTabsClient, CustomTabsSession,
// CustomTabsServiceConnection, CustomTabsCallback, EngagementSignalsCallback): a connection to
// the browser's service to warm it up, tell it which pages may open next, and hear the tab's
// navigation. A tab opened with the session's options (CustomTabsOptions::session) belongs to it.
//
//   auto pkg = ui::GetCustomTabsPackageName();
//   ui::BindCustomTabsService(*pkg, { .onConnected = [](std::shared_ptr<ui::CustomTabsClient> client) {
//       client->Warmup(0);
//       auto session = client->NewSession({ .onNavigationEvent = [](int event, const ui::Bundle&) { ... } });
//       session->MayLaunchUrl("https://example.com/help");
//   } });
//
// Callbacks run on the main thread (extraCallbackWithResult on the binder's thread: it answers).

#include "kiln/ui/CustomTabs.h"

#include <kiln/Function.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::ui {

    namespace customtabs {
        // CustomTabsCallback's navigation events.
        inline constexpr int kNavigationStarted = 1, kNavigationFinished = 2, kNavigationFailed = 3, kNavigationAborted = 4, kTabShown = 5, kTabHidden = 6;
        inline constexpr const char* kOnlineExtrasKey = "online";
        inline constexpr int kActivityLayoutStateUnknown = 0, kActivityLayoutStateBottomSheet = 1, kActivityLayoutStateBottomSheetMaximized = 2,
                             kActivityLayoutStateSideSheet = 3, kActivityLayoutStateSideSheetMaximized = 4, kActivityLayoutStateFullScreen = 5;
        // CustomTabsService's results, relations and file purposes.
        inline constexpr int kResultSuccess = 0, kResultFailureDisallowed = -1, kResultFailureRemoteError = -2, kResultFailureMessagingError = -3;
        inline constexpr int kRelationUseAsOrigin = 1, kRelationHandleAllUrls = 2;
        inline constexpr int kFilePurposeTrustedWebActivitySplashImage = 1;
        inline constexpr const char* kActionCustomTabsConnection = "android.support.customtabs.action.CustomTabsService";
        inline constexpr const char* kCategoryNavbarColorCustomization = "androidx.browser.customtabs.category.NavBarColorCustomization";
        inline constexpr const char* kCategoryColorSchemeCustomization = "androidx.browser.customtabs.category.ColorSchemeCustomization";
        inline constexpr const char* kKeyUrl = "android.support.customtabs.otherurls.URL";
        inline constexpr const char* kKeySuccess = "androidx.browser.customtabs.SUCCESS";
    }

    // CustomTabsCallback: what the tab reports. Unset functions are ignored.
    struct CustomTabsCallback {
        kiln::Function<void(int navigationEvent, const Bundle& extras)> onNavigationEvent;
        kiln::Function<void(const std::string& callbackName, const Bundle& args)> extraCallback;
        kiln::Function<std::optional<Bundle>(const std::string& callbackName, const Bundle& args)> extraCallbackWithResult;
        kiln::Function<void(const Bundle& extras)> onMessageChannelReady;
        kiln::Function<void(const std::string& message, const Bundle& extras)> onPostMessage;
        kiln::Function<void(int relation, const std::string& origin, bool result, const Bundle& extras)> onRelationshipValidationResult;
        kiln::Function<void(int height, int width, const Bundle& extras)> onActivityResized;
        kiln::Function<void(const Bundle& extras)> onWarmupCompleted;
        kiln::Function<void(int left, int top, int right, int bottom, int state, const Bundle& extras)> onActivityLayout;
        kiln::Function<void(const Bundle& extras)> onMinimized;
        kiln::Function<void(const Bundle& extras)> onUnminimized;
    };

    // EngagementSignalsCallback: how the user engages with the page.
    struct EngagementSignalsCallback {
        kiln::Function<void(bool isDirectionUp, const Bundle& extras)> onVerticalScrollEvent;
        kiln::Function<void(int scrollPercentage, const Bundle& extras)> onGreatestScrollPercentageIncreased;
        kiln::Function<void(bool didUserInteract, const Bundle& extras)> onSessionEnded;
    };

    // PrefetchOptions.
    struct PrefetchOptions {
        bool requiresAnonymousIpWhenCrossOrigin = false;
        std::optional<std::string> sourceOrigin;
    };

    class CustomTabsSession {
    public:
        CustomTabsSession(int id, std::string packageName, std::optional<int> sessionId) : m_Id(id), m_Package(std::move(packageName)), m_SessionId(sessionId) {}
        ~CustomTabsSession();
        int Id() const { return m_Id; }
        const std::string& PackageName() const { return m_Package; }
        std::optional<int> SessionId() const { return m_SessionId; }

        // The URL the user is likely to open next (warms the page up); others ranked after it as
        // bundles with customtabs::kKeyUrl.
        bool MayLaunchUrl(std::optional<std::string> url, const Bundle& extras = {}, const std::vector<Bundle>& otherLikelyBundles = {});
        void Prefetch(const std::string& url, const PrefetchOptions& options);
        void Prefetch(const std::vector<std::string>& urls, const PrefetchOptions& options);
        // updateVisuals: the open tab's action button, a toolbar item, the swipe-up gesture.
        bool SetActionButton(const CustomTabIcon& icon, const std::string& description);
        bool SetToolbarItem(int id, const CustomTabIcon& icon, const std::string& description);
        bool SetSecondaryToolbarSwipeUpGesture(std::optional<std::string> deepLink);
        // postMessage between the app and the page (after onMessageChannelReady).
        bool RequestPostMessageChannel(const std::string& postMessageOrigin, std::optional<std::string> postMessageTargetOrigin = {}, const Bundle& extras = {});
        int PostMessage(const std::string& message, const Bundle& extras = {});
        // Digital Asset Links between the app and an origin (onRelationshipValidationResult).
        bool ValidateRelationship(int relation, const std::string& origin, const Bundle& extras = {});
        bool ReceiveFile(const std::string& uri, int purpose, const Bundle& extras = {});
        // Engagement signals; std::logic_error when the browser does not support them.
        bool IsEngagementSignalsApiAvailable(const Bundle& extras = {});
        bool SetEngagementSignalsCallback(EngagementSignalsCallback callback, const Bundle& extras = {});

    private:
        Bundle Call(const std::string& method, Bundle args);
        int m_Id;
        std::string m_Package;
        std::optional<int> m_SessionId;
    };

    // AuthTabCallback: what an Auth Tab reports.
    struct AuthTabCallback {
        kiln::Function<void(int navigationEvent, const Bundle& extras)> onNavigationEvent;
        kiln::Function<void(const std::string& callbackName, const Bundle& args)> onExtraCallback;
        kiln::Function<std::optional<Bundle>(const std::string& callbackName, const Bundle& args)> onExtraCallbackWithResult;
        kiln::Function<void(const Bundle& extras)> onWarmupCompleted;
    };
    // AuthTabSession: an Auth Tab's session with a browser (AuthTabOptions::session).
    class AuthTabSession {
    public:
        AuthTabSession(int id, std::string packageName, std::optional<int> sessionId) : m_Id(id), m_Package(std::move(packageName)), m_SessionId(sessionId) {}
        int Id() const { return m_Id; }
        const std::string& PackageName() const { return m_Package; }
        std::optional<int> SessionId() const { return m_SessionId; }

    private:
        int m_Id;
        std::string m_Package;
        std::optional<int> m_SessionId;
    };
    // AuthTabSession.PendingSession (createPendingAuthTabSession).
    struct AuthTabPendingSession {
        AuthTabCallback callback;
        int id = 0;
    };
    inline AuthTabPendingSession CreatePendingAuthTabSession(int id, AuthTabCallback callback = {}) { return { std::move(callback), id }; }

    // CustomTabsSession.PendingSession (newPendingSession): a session's callback and id, made
    // before the browser is connected; a tab can open with pendingSessionId, and the session is
    // attached once connected.
    struct CustomTabsPendingSession {
        CustomTabsCallback callback;
        int id = 0;
    };
    inline CustomTabsPendingSession NewPendingSession(CustomTabsCallback callback, int id) { return { std::move(callback), id }; }

    class CustomTabsClient {
    public:
        CustomTabsClient(int connection, std::string packageName) : m_Connection(connection), m_Package(std::move(packageName)) {}
        // Starts the browser's process and loads its native code.
        bool Warmup(std::int64_t flags);
        // A session for this browser; nullptr when it refuses. With an id, the session can be
        // found again (newSession(callback, id)) and a pending session attached.
        std::shared_ptr<CustomTabsSession> NewSession(CustomTabsCallback callback, std::optional<int> id = std::nullopt);
        std::optional<Bundle> ExtraCommand(const std::string& commandName, const Bundle& args = {});
        // newAuthTabSession / attachAuthTabSession: an Auth Tab session; nullptr when refused.
        std::shared_ptr<AuthTabSession> NewAuthTabSession(AuthTabCallback callback, std::optional<int> id = std::nullopt);
        std::shared_ptr<AuthTabSession> AttachAuthTabSession(AuthTabPendingSession session) { return NewAuthTabSession(std::move(session.callback), session.id); }
        // attachSession: the pending session, now with this browser.
        std::shared_ptr<CustomTabsSession> AttachSession(CustomTabsPendingSession session) { return NewSession(std::move(session.callback), session.id); }
        const std::string& PackageName() const { return m_Package; }

    private:
        int m_Connection;
        std::string m_Package;
    };

    // CustomTabsServiceConnection.
    struct CustomTabsServiceConnection {
        kiln::Function<void(std::shared_ptr<CustomTabsClient> client)> onConnected;
        kiln::Function<void()> onDisconnected;
    };

    // bindCustomTabsService (bindCustomTabsServicePreservePriority): the binding's id, 0 when the
    // browser has no service or off Android.
    int BindCustomTabsService(const std::string& packageName, CustomTabsServiceConnection connection, bool preservePriority = false);
    void UnbindCustomTabsService(int binding);
    // getPackageName: the default browser if it supports Custom Tabs (unless ignored), else the
    // first of `packages` that does. Declare a <queries> intent for the service's action.
    std::optional<std::string> GetCustomTabsPackageName(const std::vector<std::string>& packages = {}, bool ignoreDefault = false);
    // connectAndInitialize: binds and warms the browser up.
    bool ConnectAndInitialize(const std::string& packageName);
    // Whether a browser's service declares the feature's category.
    bool IsEphemeralBrowsingSupported(const std::string& provider);
    bool IsSetNetworkSupported(const std::string& provider);
    bool IsAuthTabSupported(const std::string& provider);

    // The platform's side (dev.kiln.KilnCustomTabs on Android).
    struct CustomTabsHost {
        kiln::Function<int(const std::string& packageName, bool preservePriority)> bind;
        kiln::Function<void(int)> unbind;
        kiln::Function<std::optional<std::string>(const std::vector<std::string>&, bool)> packageName;
        kiln::Function<bool(const std::string& provider, const std::string& category)> packageHasCategory;
        kiln::Function<bool(int connection, std::int64_t flags)> warmup;
        kiln::Function<int(int connection, std::optional<int> sessionId)> newSession;
        kiln::Function<int(int connection, std::optional<int> sessionId)> newAuthTabSession;
        kiln::Function<std::optional<Bundle>(int connection, const std::string&, const Bundle&)> extraCommand;
        kiln::Function<Bundle(int session, const std::string& method, const Bundle& args)> sessionCall;
    };
    void SetCustomTabsHost(CustomTabsHost host);
    // The host's events: a binding connected or not; a session's callback (by name, its
    // arguments in a bundle); an engagement callback.
    void DispatchCustomTabsConnection(int binding, bool connected);
    void DispatchCustomTabsCallback(int session, const std::string& name, const Bundle& args);
    std::optional<Bundle> DispatchCustomTabsCallbackWithResult(int session, const std::string& name, const Bundle& args);
    void DispatchEngagementSignal(int callback, const std::string& name, const Bundle& args);

}
