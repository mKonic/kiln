#pragma once

// Sharing (Intent.createChooser, ShareCompat.IntentBuilder): text, a link or files to another
// app through the system's share sheet.
//
//   ui::Share({ .text = "https://example.com/notes/42", .subject = "A note" });
//   ui::Share({ .streams = { photoUri }, .mimeType = "image/jpeg" }, "Share the photo");

#include "kiln/ui/ActivityIntent.h"

#include <optional>
#include <string>
#include <vector>

namespace kiln::ui {

    inline constexpr const char* kActionSend = "android.intent.action.SEND";
    inline constexpr const char* kActionSendMultiple = "android.intent.action.SEND_MULTIPLE";
    inline constexpr const char* kActionChooser = "android.intent.action.CHOOSER";
    inline constexpr const char* kActionView = "android.intent.action.VIEW";
    inline constexpr const char* kExtraText = "android.intent.extra.TEXT";
    inline constexpr const char* kExtraSubject = "android.intent.extra.SUBJECT";
    inline constexpr const char* kExtraStream = "android.intent.extra.STREAM";
    inline constexpr const char* kExtraIntent = "android.intent.extra.INTENT";
    inline constexpr const char* kExtraTitle = "android.intent.extra.TITLE";
    // Intent.FLAG_GRANT_*_URI_PERMISSION and FLAG_ACTIVITY_NEW_TASK.
    inline constexpr int kFlagGrantReadUriPermission = 0x1, kFlagGrantWriteUriPermission = 0x2,
                         kFlagGrantPersistableUriPermission = 0x40, kFlagGrantPrefixUriPermission = 0x80;

    // Intent.createChooser(target, title): the system's chooser for the target. The target's URI
    // grants (its clip data, or its data) move to the chooser so the chosen app can read them.
    Intent CreateChooser(Intent target, std::optional<std::string> title = std::nullopt);

    struct ShareContent {
        std::string text;                  // EXTRA_TEXT
        std::string subject;               // EXTRA_SUBJECT
        std::vector<std::string> streams;  // content:// URIs (EXTRA_STREAM), readable by the target
        // The content's MIME type; empty: text/plain for text, */* for files.
        std::string mimeType;
    };
    // An ACTION_SEND (or SEND_MULTIPLE) intent for the content, granting read access to its URIs.
    Intent ShareIntent(const ShareContent& content);
    // Shows the share sheet. False when it could not be shown.
    inline bool Share(const ShareContent& content, std::optional<std::string> title = std::nullopt) {
        return StartActivity(CreateChooser(ShareIntent(content), std::move(title)));
    }
    // ACTION_VIEW for a URL: the browser or the app that handles the link.
    inline bool OpenUri(const std::string& uri) {
        Intent i;
        i.action = kActionView;
        i.data = uri;
        return StartActivity(i);
    }

}
