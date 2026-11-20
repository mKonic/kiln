#pragma once

// Notifications (NotificationManager, Notification.Builder): channels, then notifications the
// user taps to open the app at a deep link, with action buttons that do the same. On Android 13+
// the app declares and requests android.permission.POST_NOTIFICATIONS
// (contracts::RequestPermission); until granted, Notify posts nothing.
//
//   ui::CreateNotificationChannel({ .id = "sync", .name = "Sync" });
//   ui::Notify({ .id = 1, .channelId = "sync", .title = "Synced", .text = "12 notes", .data = "notes://list" });

#include "kiln/ui/ActivityIntent.h"
#include "kiln/ui/Person.h"

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

    // MessagingStyle.Message: a line of a conversation; no person is the user's own message.
    struct NotificationMessage {
        std::string text;
        std::int64_t timestampMillis = 0;
        std::optional<Person> person;
        bool operator==(const NotificationMessage&) const = default;
    };

    // MessagingStyle: a chat's latest messages. With a conversation shortcut (shortcutId) the
    // notification shows in the conversations section and can be a bubble.
    struct MessagingStyle {
        Person user;                     // the device's user; needs a name
        std::string conversationTitle;   // a group's name
        bool groupConversation = false;
        std::vector<NotificationMessage> messages;   // oldest first; the system shows the last 25
        bool operator==(const MessagingStyle&) const = default;
    };

    // BubbleMetadata: the conversation floats as a bubble that opens an activity in a small
    // window. With shortcutId the bubble opens that conversation shortcut's intent; without, the
    // app's launcher activity at `data`, with `icon` (empty: the app's). The activity needs
    // android:resizeableActivity="true" and android:allowEmbedded="true".
    struct BubbleMetadata {
        std::string shortcutId;
        std::string data;
        std::string icon;
        int desiredHeightDp = 0;           // 0: the system's
        bool autoExpand = false;           // opens expanded (when the app is in the foreground)
        bool suppressNotification = false; // only the bubble, no notification in the shade
        bool operator==(const BubbleMetadata&) const = default;
    };

    // Notification.CATEGORY_* the system uses to rank and filter.
    inline constexpr const char* kNotificationCategoryMessage = "msg", *kNotificationCategoryCall = "call",
                                 *kNotificationCategoryEmail = "email", *kNotificationCategorySocial = "social";

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
        // A chat: replaces bigText when set.
        std::optional<MessagingStyle> messaging;
        // The conversation shortcut it belongs to (a long-lived one; see Shortcut::isConversation).
        std::string shortcutId;
        std::string locusId;
        std::string category;            // kNotificationCategory*
        std::vector<Person> people;      // who it is about (addPerson), for ranking and Do Not Disturb
        std::optional<BubbleMetadata> bubble;
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
