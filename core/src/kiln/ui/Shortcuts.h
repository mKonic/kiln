#pragma once

// App shortcuts (ShortcutManager, ShortcutManagerCompat): actions the launcher shows on a long
// press of the app's icon, which open the app at a deep link.
//
//   ui::SetDynamicShortcuts({ { .id = "new", .shortLabel = "New note", .data = "notes://new", .icon = "ic_add" } });
//   ... in the app, the deep link arrives as the activity's intent (ui::ActivityIntent,
//   OnNewIntent, or navigation's deep links).
//
// Static shortcuts (res/xml/shortcuts.xml) need no code: name them in ACTIVITY_XML with
// <meta-data android:name="android.app.shortcuts" android:resource="@xml/shortcuts" />.
//
// Sharing shortcuts (direct share) are dynamic shortcuts whose categories match a
// <share-target> in that file; the share sheet offers them, and the activity receives the
// ACTION_SEND intent with kExtraShortcutId. Conversation shortcuts (isConversation, with
// persons) let a MessagingStyle notification with that shortcutId show as a conversation and
// a bubble.

#include "kiln/ui/ActivityIntent.h"
#include "kiln/ui/Person.h"

#include <string>
#include <vector>

namespace kiln::ui {

    struct Shortcut {
        std::string id;
        std::string shortLabel, longLabel;   // the long one when there is room
        // What it opens: `intent`, or else the app's launcher activity with ACTION_VIEW on `data`.
        std::optional<Intent> intent;
        std::string data;
        // A drawable or mipmap in the app's res/ by name; empty: the app's icon.
        std::string icon;
        int rank = 0;   // lower ranks first
        // Categories: a <share-target>'s to be a sharing shortcut (ShortcutInfo.setCategories).
        std::vector<std::string> categories;
        // The people it is with (API 29 setPersons).
        std::vector<Person> persons;
        // Kept by the system after it is removed from the dynamic list, so notifications and
        // the share sheet can still use it (setLongLived).
        bool longLived = false;
        // ShortcutInfoCompat.Builder.setIsConversation: long-lived, with the id as its locus
        // id when it has none.
        bool isConversation = false;
        std::string locusId;   // ties it to the screen showing the conversation
        bool operator==(const Shortcut&) const = default;
    };

    // ShortcutInfo.SHORTCUT_CATEGORY_CONVERSATION, and the extra a sharing shortcut's
    // ACTION_SEND carries (Intent.EXTRA_SHORTCUT_ID).
    inline constexpr const char* kShortcutCategoryConversation = "android.shortcut.conversation";
    inline constexpr const char* kExtraShortcutId = "android.intent.extra.shortcut.ID";

    // ShortcutManager.setDynamicShortcuts: replaces the app's dynamic shortcuts.
    bool SetDynamicShortcuts(const std::vector<Shortcut>& shortcuts);
    // pushDynamicShortcut: adds or updates one, dropping the lowest ranked when full, and
    // reports it used (ShortcutManagerCompat).
    bool PushDynamicShortcut(const Shortcut& shortcut);
    void RemoveDynamicShortcuts(const std::vector<std::string>& ids);
    void RemoveAllDynamicShortcuts();
    // reportShortcutUsed: the user did what the shortcut does by other means (for ranking).
    void ReportShortcutUsed(const std::string& id);
    // requestPinShortcut: asks the launcher to pin it to the home screen; false when it cannot.
    bool RequestPinShortcut(const Shortcut& shortcut);
    // The ids of the app's dynamic shortcuts.
    std::vector<std::string> DynamicShortcutIds();
    // removeLongLivedShortcuts (API 30): removes them from the dynamic list and from what the
    // system keeps for long-lived ones.
    void RemoveLongLivedShortcuts(const std::vector<std::string>& ids);

    // The host: ShortcutManager by operation.
    enum class ShortcutOp { Set, Push, Remove, RemoveAll, ReportUsed, RequestPin, List, RemoveLongLived };
    using ShortcutHost = kiln::Function<std::vector<std::string>(ShortcutOp op, const std::vector<Shortcut>& shortcuts, const std::vector<std::string>& ids)>;
    void SetShortcutHost(ShortcutHost host);

}
