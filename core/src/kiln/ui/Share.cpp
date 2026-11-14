#include "kiln/ui/Share.h"

#include <memory>

namespace kiln::ui {

    Intent CreateChooser(Intent target, std::optional<std::string> title) {
        Intent chooser;
        chooser.action = kActionChooser;
        // Migrate any clip data and flags from the target (Intent.createChooser).
        const int permFlags = target.flags & (kFlagGrantReadUriPermission | kFlagGrantWriteUriPermission |
                                              kFlagGrantPersistableUriPermission | kFlagGrantPrefixUriPermission);
        if (permFlags != 0) {
            std::vector<std::string> clip = target.clipUris;
            if (clip.empty() && target.data) clip.push_back(*target.data);
            if (!clip.empty()) {
                chooser.clipUris = std::move(clip);
                chooser.flags |= permFlags;
            }
        }
        chooser.extras.Put(kExtraIntent, BundleIntent{ std::make_shared<const Intent>(std::move(target)) });
        if (title) chooser.extras.Put(kExtraTitle, *title);
        return chooser;
    }

    Intent ShareIntent(const ShareContent& c) {
        Intent i;
        const bool multiple = c.streams.size() > 1;
        i.action = multiple ? kActionSendMultiple : kActionSend;
        i.type = !c.mimeType.empty() ? c.mimeType : c.streams.empty() ? "text/plain" : "*/*";
        if (!c.text.empty()) i.extras.Put(kExtraText, c.text);
        if (!c.subject.empty()) i.extras.Put(kExtraSubject, c.subject);
        if (multiple) i.extras.Put(kExtraStream, BundleUriList{ c.streams });
        else if (!c.streams.empty()) i.extras.Put(kExtraStream, BundleUri{ c.streams.front() });
        if (!c.streams.empty()) {
            // ShareCompat: the streams as clip data with read access, which a chooser carries on.
            i.clipUris = c.streams;
            i.flags |= kFlagGrantReadUriPermission;
        }
        return i;
    }

}
