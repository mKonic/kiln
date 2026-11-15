#pragma once

// Notifications (NotificationManager, Notification.Builder): channels, then notifications the
// user taps to open the app at a deep link, with action buttons that do the same. On Android 13+
// the app declares and requests android.permission.POST_NOTIFICATIONS
// (contracts::RequestPermission); until granted, Notify posts nothing.
//
//   ui::CreateNotificationChannel({ .id = "sync", .name = "Sync" });
//   ui::Notify({ .id = 1, .channelId = "sync", .title = "Synced", .text = "12 notes", .data = "notes://list" });

#include "kiln/ui/ActivityIntent.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kiln::ui {

    // NotificationManager.IMPORTANCE_*.
    inline constexpr int kImportanceMin = 1, kImportanceLow = 2, kImportanceDefault = 3, kImportanceHigh = 4;

    struct NotificationChannel {
        std::string id, name, description;
        int importance = kImportanceDefault;
        bool operator==(const NotificationChannel&) const = default;
    };

    struct NotificationAction {
        std::string title;
        std::string data;   // the deep link the app opens with
        std::string icon;   // a drawable in the app's res/ by name (shown on some surfaces)
        bool operator==(const NotificationAction&) const = default;
    };

    struct Notification {
        int id = 0;
        std::string tag;                 // with id, which notification an update replaces
        std::string channelId;
        std::string title, text;
        std::string bigText;             // BigTextStyle when set: the expanded text
        // A drawable in the app's res/ by name: the status bar icon (white on transparent).
        // Empty: the app's launcher icon.
        std::string smallIcon;
        std::string data;                // the deep link a tap opens; empty: the app as it was
        bool autoCancel = true;          // goes when tapped
        bool ongoing = false;            // cannot be swiped away (progress, playback)
        // Sounds and vibrates only the first time; updates (progress) arrive quietly. A
        // notification's sound itself is its channel's (its importance).
        bool onlyAlertOnce = false;
        // A progress bar: max 0 hides it; indeterminate ignores progress.
        int progress = 0, progressMax = 0;
        bool indeterminate = false;
        // Notifications of a group bundle under its summary (one with groupSummary set), which
        // shows when the group collapses (NotificationCompat.setGroup / setGroupSummary).
        std::string group;
        bool groupSummary = false;
        std::int64_t whenMillis = 0;     // shown with the notification; 0: when posted
        std::vector<NotificationAction> actions;   // up to 3 shown
        bool operator==(const Notification&) const = default;
    };

    bool CreateNotificationChannel(const NotificationChannel& channel);
    void DeleteNotificationChannel(const std::string& id);
    // Posts or updates (same id and tag). False when notifications are off for the app, the
    // channel does not exist, or off Android.
    bool Notify(const Notification& notification);
    void CancelNotification(int id, const std::string& tag = {});
    void CancelAllNotifications();
    // NotificationManager.areNotificationsEnabled (the permission and the user's switch).
    bool AreNotificationsEnabled();

    struct NotificationHost {
        kiln::Function<bool(const NotificationChannel&)> createChannel;
        kiln::Function<void(const std::string&)> deleteChannel;
        kiln::Function<bool(const Notification&)> notify;
        kiln::Function<void(int, const std::string&)> cancel;
        kiln::Function<void()> cancelAll;
        kiln::Function<bool()> enabled;
    };
    void SetNotificationHost(NotificationHost host);

}
