#include "kiln/ui/Notifications.h"

namespace kiln::ui {

    namespace {
        NotificationHost& Host() { static auto* h = new NotificationHost; return *h; }
    }

    bool CreateNotificationChannel(const NotificationChannel& c) {
        if (c.id.empty() || c.name.empty()) return false;
        return Host().createChannel && Host().createChannel(c);
    }
    void DeleteNotificationChannel(const std::string& id) { if (Host().deleteChannel) Host().deleteChannel(id); }
    bool Notify(const Notification& n) {
        if (n.channelId.empty()) return false;   // Android 8+ drops a notification without a channel
        // MessagingStyle(Person) refuses a user without a name.
        if (n.messaging && n.messaging->user.name.empty()) return false;
        return Host().notify && Host().notify(n);
    }
    void CancelNotification(int id, const std::string& tag) { if (Host().cancel) Host().cancel(id, tag); }
    void CancelAllNotifications() { if (Host().cancelAll) Host().cancelAll(); }
    bool AreNotificationsEnabled() { return Host().enabled && Host().enabled(); }
    void SetNotificationHost(NotificationHost host) { Host() = std::move(host); }

}
