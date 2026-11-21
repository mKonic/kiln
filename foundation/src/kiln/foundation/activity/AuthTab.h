#pragma once

// Auth Tab (androidx.browser.auth: AuthTabIntent, AuthTabColorSchemeParams,
// AuthenticateUserResultContract): a sign-in page in the browser that returns to the app when it
// reaches the app's redirect (a custom scheme, or an https host and path verified with Digital
// Asset Links), with the redirect's URI as the result.
//
//   auto& signIn = RememberLauncherForActivityResult(std::make_shared<contracts::AuthenticateUser>(),
//       [](const AuthResult& r) { if (r.resultCode == authtab::kResultOk) Finish(*r.resultUri); });
//   signIn.Launch(AuthTabRequest::ForScheme(AuthTabIntent({}), "https://auth.example.com/login", "myapp"));
//
// A browser without Auth Tab opens it as a Custom Tab (the result is then canceled).

#include "kiln/foundation/activity/ActivityResult.h"
#include "kiln/ui/CustomTabsClient.h"
#include "kiln/ui/Share.h"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace kiln::foundation {

    namespace authtab {
        inline constexpr int kResultCanceled = 0, kResultOk = -1, kResultVerificationFailed = 2, kResultVerificationTimedOut = 3, kResultUnknownCode = -2;
        inline constexpr const char* kExtraLaunchAuthTab = "androidx.browser.auth.extra.LAUNCH_AUTH_TAB";
        inline constexpr const char* kExtraRedirectScheme = "androidx.browser.auth.extra.REDIRECT_SCHEME";
        inline constexpr const char* kExtraHttpsRedirectHost = "androidx.browser.auth.extra.HTTPS_REDIRECT_HOST";
        inline constexpr const char* kExtraHttpsRedirectPath = "androidx.browser.auth.extra.HTTPS_REDIRECT_PATH";
    }

    // AuthTabColorSchemeParams.
    struct AuthTabColorSchemeParams {
        std::optional<ui::Color> toolbarColor, navigationBarColor, navigationBarDividerColor;
    };

    // AuthTabIntent.Builder's options.
    struct AuthTabOptions {
        std::optional<ui::Color> toolbarColor, navigationBarColor, navigationBarDividerColor;   // setDefaultColorSchemeParams
        std::map<int, AuthTabColorSchemeParams> colorSchemeParams;
        int colorScheme = ui::customtabs::kColorSchemeSystem;
        std::optional<ui::CustomTabIcon> closeButtonIcon;
        std::optional<bool> ephemeralBrowsing;
        std::shared_ptr<ui::AuthTabSession> session;
        std::optional<int> pendingSessionId;
    };

    // AuthTabIntent.Builder.build().intent.
    inline Intent AuthTabIntent(const AuthTabOptions& o) {
        Intent i;
        i.action = ui::kActionView;
        ui::Bundle& x = i.extras;
        constexpr const char* kSession = "android.support.customtabs.extra.SESSION";
        constexpr const char* kSessionId = "android.support.customtabs.extra.SESSION_ID";
        if (o.session) {
            i.packageName = o.session->PackageName();
            x.Put(kSession, ui::BundleCustomTabsSession{ o.session->Id() });
            if (o.session->SessionId()) x.Put(kSessionId, ui::BundleCustomTabsSessionId{ *o.session->SessionId() });
        } else if (o.pendingSessionId) {
            x.Put(kSession, ui::BundleNullBinder{});
            x.Put(kSessionId, ui::BundleCustomTabsSessionId{ *o.pendingSessionId });
        }
        if (o.ephemeralBrowsing) x.Put("androidx.browser.customtabs.extra.ENABLE_EPHEMERAL_BROWSING", *o.ephemeralBrowsing);
        if (o.colorScheme != ui::customtabs::kColorSchemeSystem) x.Put("androidx.browser.customtabs.extra.COLOR_SCHEME", std::int32_t(o.colorScheme));
        if (o.closeButtonIcon) x.Put("android.support.customtabs.extra.CLOSE_BUTTON_ICON", ui::BundleBitmap{ o.closeButtonIcon->encoded, o.closeButtonIcon->drawable });
        // build()
        x.Put(authtab::kExtraLaunchAuthTab, true);
        if (!x.Get(kSession)) x.Put(kSession, ui::BundleNullBinder{});   // a Custom Tab where Auth Tab is unknown
        auto colors = [](ui::Bundle& b, const std::optional<ui::Color>& toolbar, const std::optional<ui::Color>& nav, const std::optional<ui::Color>& divider) {
            if (toolbar) b.Put("android.support.customtabs.extra.TOOLBAR_COLOR", std::int32_t(toolbar->argb));
            if (nav) b.Put("androidx.browser.customtabs.extra.NAVIGATION_BAR_COLOR", std::int32_t(nav->argb));
            if (divider) b.Put("androidx.browser.customtabs.extra.NAVIGATION_BAR_DIVIDER_COLOR", std::int32_t(divider->argb));
        };
        colors(x, o.toolbarColor, o.navigationBarColor, o.navigationBarDividerColor);
        if (!o.colorSchemeParams.empty()) {
            ui::BundleSparse sparse;
            for (const auto& [scheme, p] : o.colorSchemeParams) {
                auto b = std::make_shared<ui::Bundle>();
                colors(*b, p.toolbarColor, p.navigationBarColor, p.navigationBarDividerColor);
                sparse.items.emplace_back(scheme, b);
            }
            x.Put("androidx.browser.customtabs.extra.COLOR_SCHEME_PARAMS", std::move(sparse));
        }
        return i;
    }

    // AuthTabIntent.launch's arguments: the built intent, the page, and where it returns.
    struct AuthTabRequest {
        Intent intent;
        std::string url;
        std::string redirectScheme;             // launch(launcher, url, redirectScheme)
        std::string redirectHost, redirectPath; // launch(launcher, url, redirectHost, redirectPath)
        static AuthTabRequest ForScheme(Intent intent, std::string url, std::string scheme) { return { std::move(intent), std::move(url), std::move(scheme), {}, {} }; }
        static AuthTabRequest ForHttps(Intent intent, std::string url, std::string host, std::string path) { return { std::move(intent), std::move(url), {}, std::move(host), std::move(path) }; }
    };

    // AuthTabIntent.AuthResult.
    struct AuthResult {
        int resultCode = authtab::kResultCanceled;
        std::optional<std::string> resultUri;
    };

    namespace contracts {
        // AuthenticateUserResultContract (registerActivityResultLauncher).
        struct AuthenticateUser final : ActivityResultContract<AuthTabRequest, AuthResult> {
            Intent CreateIntent(const AuthTabRequest& r) const override {
                Intent i = r.intent;
                i.data = r.url;
                if (!r.redirectScheme.empty()) {
                    i.extras.Put(authtab::kExtraRedirectScheme, r.redirectScheme);
                } else {
                    i.extras.Put(authtab::kExtraHttpsRedirectHost, r.redirectHost);
                    i.extras.Put(authtab::kExtraHttpsRedirectPath, r.redirectPath);
                }
                return i;
            }
            AuthResult ParseResult(int code, const std::optional<Intent>& intent) const override {
                switch (code) {
                    case authtab::kResultOk: return { code, intent ? intent->data : std::nullopt };
                    case authtab::kResultCanceled:
                    case authtab::kResultVerificationFailed:
                    case authtab::kResultVerificationTimedOut: return { code, std::nullopt };
                    default: return { authtab::kResultUnknownCode, std::nullopt };
                }
            }
        };
    }

}
