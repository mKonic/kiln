#include "kiln/ui/ContentResolver.h"

#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace kiln::ui {

    namespace {
        // file:// URIs (percent-decoded) and plain paths.
        std::string PathOf(std::string_view uri) {
            if (uri.starts_with("file://")) uri.remove_prefix(7);
            std::string out;
            for (std::size_t i = 0; i < uri.size(); ++i) {
                if (uri[i] == '%' && i + 2 < uri.size()) {
                    out += char(std::stoi(std::string(uri.substr(i + 1, 2)), nullptr, 16));
                    i += 2;
                } else {
                    out += uri[i];
                }
            }
            return out;
        }
        std::optional<std::string> MimeOfExtension(std::string_view name) {
            const std::size_t dot = name.rfind('.');
            if (dot == std::string_view::npos) return std::nullopt;
            std::string ext(name.substr(dot + 1));
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            static const std::pair<const char*, const char*> types[] = {
                { "txt", "text/plain" }, { "html", "text/html" }, { "htm", "text/html" }, { "css", "text/css" }, { "csv", "text/csv" },
                { "json", "application/json" }, { "xml", "text/xml" }, { "pdf", "application/pdf" }, { "zip", "application/zip" },
                { "cbz", "application/vnd.comicbook+zip" }, { "epub", "application/epub+zip" }, { "png", "image/png" },
                { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "gif", "image/gif" }, { "webp", "image/webp" },
                { "svg", "image/svg+xml" }, { "mp3", "audio/mpeg" }, { "ogg", "audio/ogg" }, { "mp4", "video/mp4" }, { "webm", "video/webm" },
            };
            for (const auto& [e, t] : types) if (ext == e) return std::string(t);
            return std::nullopt;
        }

        class FileResolver final : public ContentResolver {
        public:
            int OpenFileDescriptor(std::string_view uri, std::string_view mode) const override {
                if (uri.starts_with("content:")) return -1;
                int flags = O_CLOEXEC;
                const bool r = mode.find('r') != std::string_view::npos, w = mode.find('w') != std::string_view::npos;
                flags |= r && w ? O_RDWR : w ? O_WRONLY : O_RDONLY;
                if (w) flags |= O_CREAT;
                if (mode.find('t') != std::string_view::npos || (w && !r && mode.find('a') == std::string_view::npos)) flags |= O_TRUNC;
                if (mode.find('a') != std::string_view::npos) flags |= O_APPEND;
                return ::open(PathOf(uri).c_str(), flags, 0644);
            }
            std::optional<ContentInfo> Query(std::string_view uri) const override {
                if (uri.starts_with("content:")) return std::nullopt;
                const std::string path = PathOf(uri);
                struct stat st {};
                if (::stat(path.c_str(), &st) != 0) return std::nullopt;
                ContentInfo info;
                const std::size_t slash = path.rfind('/');
                info.displayName = slash == std::string::npos ? path : path.substr(slash + 1);
                info.size = std::int64_t(st.st_size);
                info.mimeType = MimeOfExtension(*info.displayName);
                return info;
            }
            bool TakePersistableUriPermission(std::string_view, bool, bool) const override { return true; }
            bool ReleasePersistableUriPermission(std::string_view, bool, bool) const override { return true; }
            std::vector<std::string> PersistedUriPermissions() const override { return {}; }
        };

        struct Registry {
            std::mutex mutex;
            std::shared_ptr<const ContentResolver> host;
        };
        Registry& R() {
            static Registry* r = new Registry;
            return *r;
        }
    }

    void SetContentResolver(std::shared_ptr<const ContentResolver> resolver) {
        std::lock_guard lock(R().mutex);
        R().host = std::move(resolver);
    }
    const ContentResolver& GetContentResolver() {
        static const FileResolver files;
        std::lock_guard lock(R().mutex);
        return R().host ? *R().host : files;   // set once by the host, kept for the process
    }

    std::optional<std::vector<std::uint8_t>> ReadContent(std::string_view uri) {
        const int fd = GetContentResolver().OpenFileDescriptor(uri, "r");
        if (fd < 0) return std::nullopt;
        std::vector<std::uint8_t> out;
        std::uint8_t buf[65536];
        while (true) {
            const ssize_t n = ::read(fd, buf, sizeof buf);
            if (n < 0) {
                if (errno == EINTR) continue;
                ::close(fd);
                return std::nullopt;
            }
            if (n == 0) break;
            out.insert(out.end(), buf, buf + n);
        }
        ::close(fd);
        return out;
    }

    bool WriteContent(std::string_view uri, std::span<const std::uint8_t> bytes, std::string_view mode) {
        const int fd = GetContentResolver().OpenFileDescriptor(uri, mode);
        if (fd < 0) return false;
        std::size_t done = 0;
        while (done < bytes.size()) {
            const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
            if (n < 0) {
                if (errno == EINTR) continue;
                ::close(fd);
                return false;
            }
            done += std::size_t(n);
        }
        return ::close(fd) == 0;
    }

}
