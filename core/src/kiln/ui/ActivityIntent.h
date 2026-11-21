#pragma once

// The activity's intent (Activity.getIntent) and the intents it receives while running
// (Activity.onNewIntent, ComponentActivity.addOnNewIntentListener): how a deep link, a share
// or a notification tap reaches the app. NavController reads the launch intent when its graph
// is set (as androidx's checkDeepLinkHandled); an app hands it new intents with OnNewIntent.

#include "kiln/ui/ActivityResult.h"

#include <kiln/Function.h>

#include <optional>

namespace kiln::ui {

    // The intent that started the activity; nullopt off Android or before the host sets it.
    // A new intent does not replace it (as Activity.getIntent, unless the app sets one).
    std::optional<Intent> ActivityIntent();
    void SetActivityIntent(std::optional<Intent> intent);

    // onNewIntent listeners, called on the main thread in the order added.
    int AddOnNewIntentListener(kiln::Function<void(const Intent&)> listener);
    void RemoveOnNewIntentListener(int id);
    // The host: an intent delivered to the running activity.
    void DispatchNewIntent(const Intent& intent);

    // Context.startActivity: starts an activity without waiting for a result (a share sheet, a
    // browser, another app). False when no activity handles the intent, or off Android.
    bool StartActivity(const Intent& intent);
    // ActivityOptions for startActivity(intent, options): custom enter / exit animations (by
    // resource name), sharing the app's identity (API 34) and letting touches outside pass
    // through (API 36).
    struct ActivityOptions {
        std::string enterAnimation, exitAnimation;
        std::optional<bool> shareIdentity, allowPassThroughOnTouchOutside;
        bool operator==(const ActivityOptions&) const = default;
    };
    bool StartActivity(const Intent& intent, const ActivityOptions& options);
    // Activity.shouldShowRequestPermissionRationale: the user turned the permission down
    // before without asking not to be asked again, so the app should explain why it needs it.
    bool ShouldShowRequestPermissionRationale(const std::string& permission);

    // The main activity, for the calls above.
    struct ActivityHost {
        kiln::Function<bool(const Intent&, const ActivityOptions&)> startActivity;
        kiln::Function<bool(const std::string&)> shouldShowRequestPermissionRationale;
    };
    void SetActivityHost(ActivityHost host);

}
