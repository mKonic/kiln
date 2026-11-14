#pragma once

// ContentResolver for what pickers, shares and other apps hand over: content:// URIs (and
// file:// URIs or plain paths). On Android, openFileDescriptor's descriptor (detached, so it
// is the caller's to close: read it with read(2) or fdopen), OpenableColumns' display name and
// size, getType, and persistable URI grants (takePersistableUriPermission). On the desktop, the
// same calls on files. Safe from any thread (Android attaches the calling thread to the VM), so
// large reads can run on a background dispatcher.

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kiln::ui {

    struct ContentInfo {
        std::optional<std::string> displayName;   // OpenableColumns.DISPLAY_NAME
        std::optional<std::int64_t> size;          // OpenableColumns.SIZE, in bytes
        std::optional<std::string> mimeType;       // ContentResolver.getType
    };

    class ContentResolver {
    public:
        virtual ~ContentResolver() = default;
        // mode: "r", "w" (and "wt" to truncate, "wa" to append), "rw", "rwt". -1 when the URI
        // cannot be opened (no permission, not found, a virtual file without a stream).
        virtual int OpenFileDescriptor(std::string_view uri, std::string_view mode) const = 0;
        virtual std::optional<ContentInfo> Query(std::string_view uri) const = 0;
        // takePersistableUriPermission / releasePersistableUriPermission (read and / or write
        // grants of an OpenDocument / OpenDocumentTree result kept across restarts).
        virtual bool TakePersistableUriPermission(std::string_view uri, bool read, bool write) const = 0;
        virtual bool ReleasePersistableUriPermission(std::string_view uri, bool read, bool write) const = 0;
        virtual std::vector<std::string> PersistedUriPermissions() const = 0;
    };

    void SetContentResolver(std::shared_ptr<const ContentResolver> resolver);
    // The host's, or the file-system resolver.
    const ContentResolver& GetContentResolver();

    // Conveniences over the descriptor: the whole content (nullopt when it cannot be read), and
    // writing all of `bytes` (mode "wt" replaces, "wa" appends).
    std::optional<std::vector<std::uint8_t>> ReadContent(std::string_view uri);
    bool WriteContent(std::string_view uri, std::span<const std::uint8_t> bytes, std::string_view mode = "wt");

}
