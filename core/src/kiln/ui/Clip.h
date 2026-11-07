#pragma once

// ClipEntry / ClipMetadata (androidx.compose.ui.platform, Android's ClipData and
// ClipDescription): what the clipboard, the keyboard (commitContent) or a drop hands over. Items
// carry text, HTML or a content URI; the metadata carries the MIME types.

#include <optional>
#include <string>
#include <vector>

namespace kiln::ui {

    struct ClipItem {
        std::optional<std::string> text;       // ClipData.Item.getText (UTF-8)
        std::optional<std::string> htmlText;
        std::optional<std::string> uri;        // content:// URI (read through Owner::ReadContent)
        bool operator==(const ClipItem&) const = default;
    };

    struct ClipMetadata {
        std::string label;
        std::vector<std::string> mimeTypes;
        // ClipDescription.hasMimeType / compareMimeTypes: "*/*" and "type/*" in the desired type
        // match; otherwise the types are equal.
        bool HasMimeType(const std::string& mimeType) const {
            for (const auto& t : mimeTypes) if (CompareMimeTypes(t, mimeType)) return true;
            return false;
        }
        static bool CompareMimeTypes(const std::string& concrete, const std::string& desired) {
            if (desired == "*/*") return true;
            const auto slash = desired.find('/');
            if (slash == std::string::npos || slash == 0) return false;
            if (desired.size() == slash + 2 && desired[slash + 1] == '*')
                return concrete.compare(0, slash + 1, desired, 0, slash + 1) == 0;
            return desired == concrete;
        }
        bool operator==(const ClipMetadata&) const = default;
    };

    struct ClipEntry {
        std::vector<ClipItem> items;
        bool operator==(const ClipEntry&) const = default;
    };

}
