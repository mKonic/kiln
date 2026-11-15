#include "kiln/ui/DocumentFile.h"

#include "kiln/Uri.h"

#include <algorithm>
#include <cctype>
#include <sys/stat.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace kiln::ui {

    namespace fs = std::filesystem;

    namespace {
        std::mutex g_HostMutex;
        std::shared_ptr<DocumentFile::Host>& HostSlot() {
            static auto* h = new std::shared_ptr<DocumentFile::Host>;
            return *h;
        }
        std::shared_ptr<DocumentFile::Host> GetHost() {
            std::lock_guard lock(g_HostMutex);
            return HostSlot();
        }

        // MimeTypeMap's common entries (libcore's MimeMap), extension first as getExtensionFromMimeType gives it.
        struct MimeEntry { const char* extension; const char* mime; };
        constexpr MimeEntry kMimes[] = {
            { "txt", "text/plain" }, { "html", "text/html" }, { "htm", "text/html" }, { "css", "text/css" },
            { "csv", "text/comma-separated-values" }, { "xml", "text/xml" }, { "json", "application/json" },
            { "pdf", "application/pdf" }, { "zip", "application/zip" }, { "apk", "application/vnd.android.package-archive" },
            { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "gif", "image/gif" },
            { "webp", "image/webp" }, { "bmp", "image/bmp" }, { "svg", "image/svg+xml" }, { "heic", "image/heic" },
            { "mp3", "audio/mpeg" }, { "m4a", "audio/mp4" }, { "ogg", "audio/ogg" }, { "wav", "audio/x-wav" },
            { "flac", "audio/flac" }, { "mp4", "video/mp4" }, { "webm", "video/webm" }, { "3gp", "video/3gpp" },
            { "mkv", "video/x-matroska" },
        };
        std::optional<std::string> ExtensionFromMimeType(const std::string& mime) {
            for (const auto& e : kMimes) if (mime == e.mime) return std::string(e.extension);
            return std::nullopt;
        }
        // RawDocumentFile.getTypeForName.
        std::string TypeForName(const std::string& name) {
            if (const auto dot = name.rfind('.'); dot != std::string::npos) {
                std::string ext = name.substr(dot + 1);
                for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
                for (const auto& e : kMimes) if (ext == e.extension) return e.mime;
            }
            return "application/octet-stream";
        }
        std::string FileUri(const std::string& path) { return "file://" + Uri::Encode(path, "/"); }
        std::string PathOf(const std::string& fileUri) { return Uri::Decode(fileUri.substr(7)); }
        bool DeleteContents(const fs::path& dir) {
            std::error_code ec;
            bool ok = true;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.is_directory(ec)) ok &= DeleteContents(entry.path());
                if (!fs::remove(entry.path(), ec)) ok = false;
            }
            return ok;
        }
        void Unsupported() { throw std::logic_error("UnsupportedOperationException: a single document has no children and cannot be renamed"); }
    }

    void DocumentFile::SetHost(std::shared_ptr<Host> host) {
        std::lock_guard lock(g_HostMutex);
        HostSlot() = std::move(host);
    }

    bool DocumentFile::IsDocumentUri(const std::string& uri) {
        auto h = GetHost();
        return h && h->IsDocumentUri(uri);
    }

    std::optional<DocumentFile> DocumentFile::FromTreeUri(const std::string& treeUri) {
        auto h = GetHost();
        if (!h) return std::nullopt;
        auto documentUri = h->TreeDocumentUri(treeUri);
        if (!documentUri) return std::nullopt;
        return DocumentFile(Kind::Tree, *documentUri, nullptr);
    }
    DocumentFile DocumentFile::FromSingleUri(const std::string& uri) { return DocumentFile(Kind::Single, uri, nullptr); }
    DocumentFile DocumentFile::FromFile(const std::string& path) { return DocumentFile(Kind::Raw, FileUri(path), nullptr); }

    std::optional<DocumentFile::DocumentInfo> DocumentFile::Info() const {
        auto h = GetHost();
        return h ? h->Query(m_Uri) : std::nullopt;
    }
    std::optional<std::string> DocumentFile::RawType() const {
        auto info = Info();
        return info ? info->mimeType : std::nullopt;
    }

    std::optional<DocumentFile> DocumentFile::CreateFile(const std::string& mimeType, const std::string& displayName) const {
        if (m_Kind == Kind::Single) Unsupported();
        auto parent = std::make_shared<const DocumentFile>(*this);
        if (m_Kind == Kind::Raw) {
            std::string name = displayName;
            if (auto ext = ExtensionFromMimeType(mimeType)) name += "." + *ext;   // tack on the extension
            const fs::path target = fs::path(PathOf(m_Uri)) / name;
            std::error_code ec;
            if (fs::exists(target, ec)) return std::nullopt;   // createNewFile: false when it exists
            if (!std::ofstream(target).good()) return std::nullopt;
            return DocumentFile(Kind::Raw, FileUri(target.string()), parent);
        }
        auto h = GetHost();
        if (!h) return std::nullopt;
        auto uri = h->Create(m_Uri, mimeType, displayName);
        if (!uri) return std::nullopt;
        return DocumentFile(Kind::Tree, *uri, parent);
    }

    std::optional<DocumentFile> DocumentFile::CreateDirectory(const std::string& displayName) const {
        if (m_Kind == Kind::Single) Unsupported();
        auto parent = std::make_shared<const DocumentFile>(*this);
        if (m_Kind == Kind::Raw) {
            const fs::path target = fs::path(PathOf(m_Uri)) / displayName;
            std::error_code ec;
            if (fs::is_directory(target, ec) || fs::create_directory(target, ec)) return DocumentFile(Kind::Raw, FileUri(target.string()), parent);
            return std::nullopt;
        }
        auto h = GetHost();
        if (!h) return std::nullopt;
        auto uri = h->Create(m_Uri, kMimeTypeDir, displayName);
        if (!uri) return std::nullopt;
        return DocumentFile(Kind::Tree, *uri, parent);
    }

    std::optional<std::string> DocumentFile::Name() const {
        if (m_Kind == Kind::Raw) return fs::path(PathOf(m_Uri)).filename().string();
        auto info = Info();
        return info ? info->displayName : std::nullopt;
    }

    std::optional<std::string> DocumentFile::Type() const {
        if (m_Kind == Kind::Raw) {
            std::error_code ec;
            if (fs::is_directory(PathOf(m_Uri), ec)) return std::nullopt;
            return TypeForName(fs::path(PathOf(m_Uri)).filename().string());
        }
        auto raw = RawType();
        if (raw && *raw == kMimeTypeDir) return std::nullopt;
        return raw;
    }

    bool DocumentFile::IsDirectory() const {
        std::error_code ec;
        if (m_Kind == Kind::Raw) return fs::is_directory(PathOf(m_Uri), ec);
        auto raw = RawType();
        return raw && *raw == kMimeTypeDir;
    }

    bool DocumentFile::IsFile() const {
        std::error_code ec;
        if (m_Kind == Kind::Raw) return fs::is_regular_file(PathOf(m_Uri), ec);
        auto raw = RawType();
        return raw && !raw->empty() && *raw != kMimeTypeDir;
    }

    bool DocumentFile::IsVirtual() const {
        if (m_Kind == Kind::Raw || !IsDocumentUri(m_Uri)) return false;
        auto info = Info();
        return info && (info->flags & kFlagVirtualDocument) != 0;
    }

    std::int64_t DocumentFile::LastModified() const {
        if (m_Kind == Kind::Raw) {
            // File.lastModified: the modification time in ms, 0 when it cannot be read.
            struct stat st{};
            if (::stat(PathOf(m_Uri).c_str(), &st) != 0) return 0;
            return std::int64_t(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
        }
        auto info = Info();
        return info ? info->lastModified : 0;
    }

    std::int64_t DocumentFile::Length() const {
        if (m_Kind == Kind::Raw) {
            std::error_code ec;
            const auto n = fs::file_size(PathOf(m_Uri), ec);
            return ec ? 0 : std::int64_t(n);
        }
        auto info = Info();
        return info ? info->size : 0;
    }

    bool DocumentFile::CanRead() const {
        if (m_Kind == Kind::Raw) return ::access(PathOf(m_Uri).c_str(), R_OK) == 0;
        // DocumentsContractApi19.canRead: the grant, and a MIME type.
        auto h = GetHost();
        if (!h || !h->HasPermission(m_Uri, false)) return false;
        auto raw = RawType();
        return raw && !raw->empty();
    }

    bool DocumentFile::CanWrite() const {
        if (m_Kind == Kind::Raw) return ::access(PathOf(m_Uri).c_str(), W_OK) == 0;
        auto h = GetHost();
        if (!h || !h->HasPermission(m_Uri, true)) return false;
        auto info = Info();
        if (!info || !info->mimeType || info->mimeType->empty()) return false;
        // Deletable documents count as writable; directories that take new documents, and
        // documents that can be written.
        if (info->flags & kFlagSupportsDelete) return true;
        if (*info->mimeType == kMimeTypeDir) return (info->flags & kFlagDirSupportsCreate) != 0;
        return (info->flags & kFlagSupportsWrite) != 0;
    }

    bool DocumentFile::Delete() const {
        if (m_Kind == Kind::Raw) {
            const fs::path p = PathOf(m_Uri);
            DeleteContents(p);
            std::error_code ec;
            return fs::remove(p, ec);
        }
        auto h = GetHost();
        return h && h->Delete(m_Uri);
    }

    bool DocumentFile::Exists() const {
        std::error_code ec;
        if (m_Kind == Kind::Raw) return fs::exists(PathOf(m_Uri), ec);
        return Info().has_value();
    }

    std::vector<DocumentFile> DocumentFile::ListFiles() const {
        if (m_Kind == Kind::Single) Unsupported();
        std::vector<DocumentFile> out;
        auto parent = std::make_shared<const DocumentFile>(*this);
        if (m_Kind == Kind::Raw) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(PathOf(m_Uri), ec)) out.push_back(DocumentFile(Kind::Raw, FileUri(entry.path().string()), parent));
            return out;
        }
        auto h = GetHost();
        if (!h) return out;
        for (auto& uri : h->Children(m_Uri)) out.push_back(DocumentFile(Kind::Tree, std::move(uri), parent));
        return out;
    }

    std::optional<DocumentFile> DocumentFile::FindFile(const std::string& displayName) const {
        for (DocumentFile& d : ListFiles())
            if (d.Name() == displayName) return std::move(d);
        return std::nullopt;
    }

    bool DocumentFile::RenameTo(const std::string& displayName) {
        if (m_Kind == Kind::Single) Unsupported();
        if (m_Kind == Kind::Raw) {
            const fs::path from = PathOf(m_Uri);
            const fs::path to = from.parent_path() / displayName;
            std::error_code ec;
            fs::rename(from, to, ec);
            if (ec) return false;
            m_Uri = FileUri(to.string());
            return true;
        }
        auto h = GetHost();
        if (!h) return false;
        auto uri = h->Rename(m_Uri, displayName);
        if (!uri) return false;
        m_Uri = *uri;
        return true;
    }

    std::optional<DocumentFile> DocumentFile::ParentFile() const {
        if (!m_Parent) return std::nullopt;
        return *m_Parent;
    }

}
