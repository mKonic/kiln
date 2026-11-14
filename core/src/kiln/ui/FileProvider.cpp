#include "kiln/ui/FileProvider.h"

#include "kiln/Uri.h"

#include <filesystem>
#include <mutex>
#include <stdexcept>

namespace kiln::ui {

    namespace {
        struct Provider {
            std::mutex mutex;
            std::string authority;
            std::vector<std::pair<std::string, std::string>> roots;   // canonical
        };
        Provider& P() {
            static Provider* p = new Provider;
            return *p;
        }
        std::string Canonical(const std::string& path) {
            std::error_code ec;
            std::filesystem::path p = std::filesystem::weakly_canonical(path, ec);
            if (ec) throw std::invalid_argument("Failed to resolve canonical path for " + path);
            // File.getCanonicalPath: no trailing separator.
            std::string out = p.string();
            while (out.size() > 1 && out.back() == '/') out.pop_back();
            return out;
        }
        std::string WithoutTrailingSlash(std::string s) {
            if (s.size() > 1 && s.back() == '/') s.pop_back();
            return s;
        }
        bool BelongsToRoot(const std::string& file, const std::string& root) {
            return WithoutTrailingSlash(file).starts_with(WithoutTrailingSlash(root) + '/');
        }
    }

    void SetFileProvider(std::string authority, std::vector<std::pair<std::string, std::string>> roots) {
        std::lock_guard lock(P().mutex);
        P().authority = std::move(authority);
        P().roots.clear();
        for (auto& [name, dir] : roots) {
            if (name.empty()) throw std::invalid_argument("Name must not be empty");
            if (!dir.empty()) P().roots.emplace_back(name, Canonical(dir));
        }
    }

    std::string GetUriForFile(const std::string& file, const std::string& displayName) {
        std::lock_guard lock(P().mutex);
        std::string path = Canonical(file);
        if (P().authority.empty()) return "file://" + Uri::Encode(path, "/");
        // The most specific root that holds it.
        const std::pair<std::string, std::string>* best = nullptr;
        for (const auto& root : P().roots)
            if (BelongsToRoot(path, root.second) && (!best || root.second.size() > best->second.size())) best = &root;
        if (!best) throw std::invalid_argument("Failed to find configured root that contains " + path);
        const std::string& rootPath = best->second;
        path = rootPath.ends_with('/') ? path.substr(rootPath.size()) : path.substr(rootPath.size() + 1);
        std::string uri = "content://" + P().authority + "/" + Uri::Encode(best->first) + "/" + Uri::Encode(path, "/");
        // getUriForFile(context, authority, file, displayName): appendQueryParameter.
        if (!displayName.empty()) uri += "?displayName=" + Uri::Encode(displayName);
        return uri;
    }

    std::string GetFileForUri(const std::string& uri) {
        std::lock_guard lock(P().mutex);
        // The encoded path: after scheme://authority, before ? or #.
        std::size_t start = uri.find("://");
        start = start == std::string::npos ? 0 : uri.find('/', start + 3);
        if (start == std::string::npos) throw std::invalid_argument("Unable to find path from root: " + uri);
        std::string path = uri.substr(start, uri.find_first_of("?#", start) - start);
        const std::size_t split = path.find('/', 1);
        if (split == std::string::npos) throw std::invalid_argument("Unable to find path from root: " + uri);
        const std::string tag = Uri::Decode(path.substr(1, split - 1));
        path = Uri::Decode(path.substr(split + 1));
        const std::string* root = nullptr;
        for (const auto& r : P().roots)
            if (r.first == tag) root = &r.second;
        if (!root) throw std::invalid_argument("Unable to find configured root for " + uri);
        const std::string file = Canonical((std::filesystem::path(*root) / path).string());
        if (!BelongsToRoot(file, *root)) throw std::runtime_error("Resolved path jumped beyond configured root");
        return file;
    }

}
