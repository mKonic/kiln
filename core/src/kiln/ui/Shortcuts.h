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

#include "kiln/ui/ActivityIntent.h"

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
        bool operator==(const Shortcut&) const = default;
    };

    // ShortcutManager.setDynamicShortcuts: replaces the app's dynamic shortcuts.
    bool SetDynamicShortcuts(const std::vector<Shortcut>& shortcuts);
    // pushDynamicShortcut: adds or updates one, dropping the lowest ranked when full.
    bool PushDynamicShortcut(const Shortcut& shortcut);
    void RemoveDynamicShortcuts(const std::vector<std::string>& ids);
    void RemoveAllDynamicShortcuts();
    // reportShortcutUsed: the user did what the shortcut does by other means (for ranking).
    void ReportShortcutUsed(const std::string& id);
    // requestPinShortcut: asks the launcher to pin it to the home screen; false when it cannot.
    bool RequestPinShortcut(const Shortcut& shortcut);
    // The ids of the app's dynamic shortcuts.
    std::vector<std::string> DynamicShortcutIds();

    // The host: ShortcutManager by operation.
    enum class ShortcutOp { Set, Push, Remove, RemoveAll, ReportUsed, RequestPin, List };
    using ShortcutHost = kiln::Function<std::vector<std::string>(ShortcutOp op, const std::vector<Shortcut>& shortcuts, const std::vector<std::string>& ids)>;
    void SetShortcutHost(ShortcutHost host);

}
