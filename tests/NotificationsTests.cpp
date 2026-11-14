// Notifications: the checks before the host, and what reaches it.
#include "Check.h"

#include "kiln/ui/Notifications.h"

using namespace kiln;
using namespace kiln::ui;

TEST(NotificationsCheckTheirChannelAndReachTheHost) {
    SetNotificationHost({});
    CHECK(!Notify({ .id = 1, .channelId = "c" }) && !AreNotificationsEnabled());
    std::vector<NotificationChannel> channels;
    std::vector<Notification> posted;
    std::vector<std::pair<int, std::string>> cancelled;
    SetNotificationHost({
        .createChannel = [&](const NotificationChannel& c) { channels.push_back(c); return true; },
        .notify = [&](const Notification& n) { posted.push_back(n); return true; },
        .cancel = [&](int id, const std::string& tag) { cancelled.emplace_back(id, tag); },
        .enabled = [] { return true; },
    });
    CHECK(!CreateNotificationChannel({ .id = "sync" }));   // a channel needs a name
    CHECK(CreateNotificationChannel({ .id = "sync", .name = "Sync", .importance = kImportanceLow }));
    CHECK(!Notify({ .id = 1, .title = "no channel" }));
    CHECK(Notify({ .id = 2, .channelId = "sync", .title = "Syncing", .onlyAlertOnce = true, .progress = 3, .progressMax = 10,
                   .actions = { { .title = "Open", .data = "notes://sync" } } }));
    CancelNotification(2, "t");
    CHECK(channels.size() == 1 && channels[0].importance == kImportanceLow);
    CHECK(posted.size() == 1 && posted[0].progressMax == 10 && posted[0].actions.size() == 1 && posted[0].actions[0].data == "notes://sync");
    CHECK(cancelled == (std::vector<std::pair<int, std::string>>{ { 2, "t" } }) && AreNotificationsEnabled());
    SetNotificationHost({});
}
