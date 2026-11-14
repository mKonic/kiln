#include "kiln/image/ImageLoader.h"

#include "kiln/Assets.h"
#include "kiln/runtime/TaskDispatchers.h"
#include "kiln/ui/AppDirectories.h"
#include "kiln/ui/ContentResolver.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

namespace kiln::image {

    namespace {
        std::atomic<std::int64_t> g_MemoryClass{ -1 };
        std::atomic<bool> g_LowRam{ false };

        bool StartsWith(std::string_view s, std::string_view prefix) {
            if (s.size() < prefix.size()) return false;
            for (std::size_t i = 0; i < prefix.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
            return true;
        }

        std::optional<std::vector<std::uint8_t>> ReadWholeFile(const std::string& path) {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) return std::nullopt;
            std::vector<std::uint8_t> out;
            std::uint8_t buf[65536];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
            std::fclose(f);
            return out;
        }

        void MakeDirs(const std::string& path) {
            for (std::size_t i = 1; i <= path.size(); ++i)
                if (i == path.size() || path[i] == '/') ::mkdir(path.substr(0, i).c_str(), 0700);
        }

        int Base64Value(char c) {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+' || c == '-') return 62;
            if (c == '/' || c == '_') return 63;
            return -1;
        }

        // data:[<mediatype>][;base64],<data> (DataUriFetcher).
        std::optional<std::vector<std::uint8_t>> DecodeDataUri(std::string_view uri) {
            const auto comma = uri.find(',');
            if (comma == std::string_view::npos) return std::nullopt;
            const std::string_view meta = uri.substr(5, comma - 5), payload = uri.substr(comma + 1);
            std::vector<std::uint8_t> out;
            if (meta.ends_with(";base64")) {
                unsigned acc = 0;
                int bits = 0;
                for (char c : payload) {
                    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
                    const int v = Base64Value(c);
                    if (v < 0) return std::nullopt;
                    acc = (acc << 6) | unsigned(v);
                    bits += 6;
                    if (bits >= 8) {
                        bits -= 8;
                        out.push_back(std::uint8_t((acc >> bits) & 0xff));
                    }
                }
            } else {
                for (std::size_t i = 0; i < payload.size(); ++i) {
                    if (payload[i] == '%' && i + 2 < payload.size()) {
                        out.push_back(std::uint8_t(std::stoi(std::string(payload.substr(i + 1, 2)), nullptr, 16)));
                        i += 2;
                    } else {
                        out.push_back(std::uint8_t(payload[i]));
                    }
                }
            }
            return out;
        }

        std::string Hex(std::uint64_t v) {
            static const char* digits = "0123456789abcdef";
            std::string s(16, '0');
            for (int i = 15; i >= 0; --i, v >>= 4) s[std::size_t(i)] = digits[v & 15];
            return s;
        }
        std::uint64_t Fnv(std::string_view s, std::uint64_t h) {
            for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ULL; }
            return h;
        }

        struct Fetched {
            std::vector<std::uint8_t> bytes;
            DataSource source = DataSource::Memory;
            std::string diskCacheKey;
        };
        struct Decoded {
            gfx::ImageBitmap image;
            bool sampled = false;
        };
    }

    void SetMemoryClass(std::int64_t bytes, bool lowRam) {
        g_MemoryClass = bytes;
        g_LowRam = lowRam;
    }

    // --- DecodeUtils ----------------------------------------------------------------------------

    std::pair<int, int> DecodeSize(int srcWidth, int srcHeight, const ImageRequest& r, bool* sampled) {
        if (sampled) *sampled = false;
        if (srcWidth <= 0 || srcHeight <= 0) return { srcWidth, srcHeight };
        const bool fill = r.scale == Scale::Fill;
        // computeDstSize: an undefined side is MIN_VALUE for FILL, MAX_VALUE for FIT.
        const auto px = [fill](int v) -> long long { return v > 0 ? v : (fill ? INT_MIN : INT_MAX); };
        long long dstW = srcWidth, dstH = srcHeight;
        if (r.width > 0 || r.height > 0) { dstW = px(r.width); dstH = px(r.height); }
        const auto minOrMax = [](long long v) { return v == INT_MIN || v == INT_MAX; };
        if (r.maxWidth > 0 && !minOrMax(dstW)) dstW = std::min<long long>(dstW, r.maxWidth);
        if (r.maxHeight > 0 && !minOrMax(dstH)) dstH = std::min<long long>(dstH, r.maxHeight);
        if (dstW == srcWidth && dstH == srcHeight) return { srcWidth, srcHeight };
        // computeSizeMultiplier.
        const double wp = double(dstW) / srcWidth, hp = double(dstH) / srcHeight;
        double percent = fill ? std::max(wp, hp) : std::min(wp, hp);
        if (r.maxWidth > 0) percent = std::min(percent, double(r.maxWidth) / srcWidth);
        if (r.maxHeight > 0) percent = std::min(percent, double(r.maxHeight) / srcHeight);
        const bool isSampled = percent < 1;
        if (sampled) *sampled = isSampled;
        if (isSampled || r.precision == Precision::Exact)
            return { std::max(1, int(std::lround(percent * srcWidth))), std::max(1, int(std::lround(percent * srcHeight))) };
        return { srcWidth, srcHeight };
    }

    bool CachedSizeSatisfies(const MemoryCache::Value& value, const ImageRequest& r) {
        const bool original = r.width <= 0 && r.height <= 0;
        if (!value.sampled && (original || r.precision == Precision::Inexact)) return true;
        const int srcW = value.image.Width(), srcH = value.image.Height();
        if (srcW <= 0 || srcH <= 0) return false;
        const long long dstW = std::min<long long>(r.width > 0 ? r.width : INT_MAX, r.maxWidth > 0 ? r.maxWidth : INT_MAX);
        const long long dstH = std::min<long long>(r.height > 0 ? r.height : INT_MAX, r.maxHeight > 0 ? r.maxHeight : INT_MAX);
        const double wp = double(dstW) / srcW, hp = double(dstH) / srcH;
        const bool fill = dstW != INT_MAX && dstH != INT_MAX && r.scale == Scale::Fill;
        double multiplier;
        long long difference;
        if (fill ? wp > hp : wp < hp) { multiplier = wp; difference = std::llabs(dstW - srcW); }
        else { multiplier = hp; difference = std::llabs(dstH - srcH); }
        // One pixel of tolerance for downsampling's rounding.
        if (difference <= 1) return true;
        return r.precision == Precision::Exact ? multiplier == 1.0 : multiplier <= 1.0;
    }

    // --- MemoryCache ----------------------------------------------------------------------------

    const MemoryCache::Value* MemoryCache::Get(const std::string& key) {
        auto it = m_Map.find(key);
        if (it == m_Map.end()) return nullptr;
        m_Order.splice(m_Order.begin(), m_Order, it->second);
        return &it->second->second;
    }
    void MemoryCache::Set(const std::string& key, Value value) {
        Remove(key);
        const std::size_t bytes = value.image.ByteCount();
        if (bytes > m_Max) return;   // larger than the whole cache: not kept (LruCache)
        m_Order.emplace_front(key, std::move(value));
        m_Map[key] = m_Order.begin();
        m_Size += bytes;
        TrimToSize(m_Max);
    }
    bool MemoryCache::Remove(const std::string& key) {
        auto it = m_Map.find(key);
        if (it == m_Map.end()) return false;
        m_Size -= it->second->second.image.ByteCount();
        m_Order.erase(it->second);
        m_Map.erase(it);
        return true;
    }
    void MemoryCache::Clear() {
        m_Order.clear();
        m_Map.clear();
        m_Size = 0;
    }
    void MemoryCache::TrimToSize(std::size_t bytes) {
        while (m_Size > bytes && !m_Order.empty()) {
            m_Size -= m_Order.back().second.image.ByteCount();
            m_Map.erase(m_Order.back().first);
            m_Order.pop_back();
        }
    }

    // --- DiskCache ------------------------------------------------------------------------------

    DiskCache::DiskCache(std::string directory, std::int64_t maxBytes) : m_Dir(std::move(directory)), m_Max(maxBytes) {
        MakeDirs(m_Dir);
        if (m_Max <= 0) {
            // DiskCache.Builder: 2% of the free space, between 10 and 250 MB.
            struct statvfs fs{};
            const double free = ::statvfs(m_Dir.c_str(), &fs) == 0 ? double(fs.f_bavail) * double(fs.f_frsize) : 0.0;
            m_Max = std::clamp<std::int64_t>(std::int64_t(free * 0.02), 10LL * 1024 * 1024, 250LL * 1024 * 1024);
        }
        // Edits a previous process left unfinished.
        if (DIR* d = ::opendir(m_Dir.c_str())) {
            while (dirent* e = ::readdir(d))
                if (std::string_view(e->d_name).find(".tmp") != std::string_view::npos) ::unlink((m_Dir + "/" + e->d_name).c_str());
            ::closedir(d);
        }
    }
    std::string DiskCache::PathOf(const std::string& key) const {
        return m_Dir + "/" + Hex(Fnv(key, 0xcbf29ce484222325ULL)) + Hex(Fnv(key, 0x84222325cbf29ce4ULL));
    }
    std::string DiskCache::Get(const std::string& key) {
        std::lock_guard lock(m_Mutex);
        const std::string path = PathOf(key);
        if (::access(path.c_str(), R_OK) != 0) return {};
        ::utimensat(AT_FDCWD, path.c_str(), nullptr, 0);   // its use time, for the LRU order
        return path;
    }
    std::string DiskCache::EditFile(const std::string& key) {
        std::lock_guard lock(m_Mutex);
        MakeDirs(m_Dir);
        return PathOf(key) + ".tmp" + std::to_string(++m_NextEdit);
    }
    std::string DiskCache::Commit(const std::string& key, const std::string& editFile) {
        std::lock_guard lock(m_Mutex);
        const std::string path = PathOf(key);
        if (std::rename(editFile.c_str(), path.c_str()) != 0) {
            ::unlink(editFile.c_str());
            return {};
        }
        Trim();
        return ::access(path.c_str(), R_OK) == 0 ? path : std::string();
    }
    void DiskCache::Abort(const std::string& editFile) { ::unlink(editFile.c_str()); }
    bool DiskCache::Remove(const std::string& key) {
        std::lock_guard lock(m_Mutex);
        return ::unlink(PathOf(key).c_str()) == 0;
    }
    void DiskCache::Clear() {
        std::lock_guard lock(m_Mutex);
        if (DIR* d = ::opendir(m_Dir.c_str())) {
            while (dirent* e = ::readdir(d))
                if (e->d_name[0] != '.' && std::string_view(e->d_name).find(".tmp") == std::string_view::npos) ::unlink((m_Dir + "/" + e->d_name).c_str());
            ::closedir(d);
        }
    }
    std::int64_t DiskCache::Size() {
        std::lock_guard lock(m_Mutex);
        std::int64_t total = 0;
        if (DIR* d = ::opendir(m_Dir.c_str())) {
            while (dirent* e = ::readdir(d)) {
                if (e->d_name[0] == '.' || std::string_view(e->d_name).find(".tmp") != std::string_view::npos) continue;
                struct stat st{};
                if (::stat((m_Dir + "/" + e->d_name).c_str(), &st) == 0) total += st.st_size;
            }
            ::closedir(d);
        }
        return total;
    }
    // With the lock: drops the least recently used entries beyond the maximum.
    void DiskCache::Trim() {
        struct Item { std::string path; std::int64_t size; timespec used; };
        std::vector<Item> items;
        std::int64_t total = 0;
        if (DIR* d = ::opendir(m_Dir.c_str())) {
            while (dirent* e = ::readdir(d)) {
                if (e->d_name[0] == '.' || std::string_view(e->d_name).find(".tmp") != std::string_view::npos) continue;
                std::string path = m_Dir + "/" + e->d_name;
                struct stat st{};
                if (::stat(path.c_str(), &st) != 0) continue;
                items.push_back({ std::move(path), st.st_size, st.st_mtim });
                total += st.st_size;
            }
            ::closedir(d);
        }
        if (total <= m_Max) return;
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            return a.used.tv_sec != b.used.tv_sec ? a.used.tv_sec < b.used.tv_sec : a.used.tv_nsec < b.used.tv_nsec;
        });
        for (const Item& it : items) {
            if (total <= m_Max) break;
            if (::unlink(it.path.c_str()) == 0) total -= it.size;
        }
    }

    // --- ImageLoader ----------------------------------------------------------------------------

    namespace {
        std::size_t DefaultMemoryBytes() {
            const std::int64_t cls = g_MemoryClass.load();
            if (cls > 0) return std::size_t(double(cls) * (g_LowRam ? 0.15 : 0.2));
            return std::size_t(512.0 * 1024 * 1024 * 0.15);
        }
    }

    ImageLoader::ImageLoader() : ImageLoader(Options{}) {}
    ImageLoader::ImageLoader(Options options)
        : m_Options(std::move(options)), m_Memory(m_Options.memoryCacheBytes ? m_Options.memoryCacheBytes : DefaultMemoryBytes()) {}

    ImageLoader& ImageLoader::Default() {
        static auto* loader = new ImageLoader();
        return *loader;
    }

    DiskCache& ImageLoader::Disk() {
        std::lock_guard lock(m_DiskMutex);
        if (!m_Disk) {
            std::string dir = m_Options.diskCacheDirectory.empty() ? ui::CacheDir() + "/image_cache" : m_Options.diskCacheDirectory;
            m_Disk = std::make_unique<DiskCache>(std::move(dir), m_Options.diskCacheBytes);
        }
        return *m_Disk;
    }

    void ImageLoader::TrimMemory(int level) {
        // RealMemoryCache.trimMemory with ComponentCallbacks2's levels.
        if (level >= 40) m_Memory.Clear();
        else if (level >= 10 && level < 20) m_Memory.TrimToSize(m_Memory.Size() / 2);
    }

    namespace {
        rt::Task<Fetched> FetchNetwork(ImageLoader* loader, const ImageRequest& r) {
            auto& io = rt::TaskDispatchers::Platform().IO();
            const std::string key = r.diskCacheKey.empty() ? r.data : r.diskCacheKey;
            DiskCache& disk = loader->Disk();
            if (r.diskCachePolicy.read) {
                std::optional<std::vector<std::uint8_t>> cached = co_await rt::WithContext(io, [&disk, key]() -> std::optional<std::vector<std::uint8_t>> {
                    const std::string path = disk.Get(key);
                    return path.empty() ? std::nullopt : ReadWholeFile(path);
                });
                if (cached) co_return Fetched{ std::move(*cached), DataSource::Disk, key };
            }
            if (!r.networkCachePolicy.read) throw std::runtime_error("not in the disk cache, and the network is not allowed");
            net::HttpRequest request;
            request.url = r.data;
            request.headers = r.headers;
            const bool write = r.diskCachePolicy.write;
            if (write) request.responseFile = co_await rt::WithContext(io, [&disk, key] { return disk.EditFile(key); });
            net::HttpResponse response = co_await net::Fetch(request);
            if (!response.Ok()) {
                if (write) disk.Abort(request.responseFile);
                throw std::runtime_error("HTTP " + std::to_string(response.status) + (response.statusText.empty() ? "" : " " + response.statusText));
            }
            if (!write) co_return Fetched{ std::vector<std::uint8_t>(response.body.begin(), response.body.end()), DataSource::Network, {} };
            const std::string edit = request.responseFile;
            std::optional<std::vector<std::uint8_t>> bytes = co_await rt::WithContext(io, [&disk, key, edit]() -> std::optional<std::vector<std::uint8_t>> {
                const std::string path = disk.Commit(key, edit);
                return path.empty() ? std::nullopt : ReadWholeFile(path);
            });
            if (!bytes) throw std::runtime_error("cannot read the downloaded image");
            co_return Fetched{ std::move(*bytes), DataSource::Network, key };
        }

        rt::Task<Fetched> FetchData(ImageLoader* loader, const ImageRequest& r) {
            const std::string& data = r.data;
            if (StartsWith(data, "http://") || StartsWith(data, "https://")) co_return co_await FetchNetwork(loader, r);
            if (StartsWith(data, "data:")) {
                auto bytes = DecodeDataUri(data);
                if (!bytes) throw std::runtime_error("malformed data URI");
                co_return Fetched{ std::move(*bytes), DataSource::Memory, {} };
            }
            auto& io = rt::TaskDispatchers::Platform().IO();
            std::string asset;
            if (StartsWith(data, "asset:///")) asset = data.substr(9);
            else if (StartsWith(data, "file:///android_asset/")) asset = data.substr(22);
            std::optional<std::vector<std::uint8_t>> bytes;
            if (!asset.empty()) bytes = co_await rt::WithContext(io, [asset] { return ReadAsset(asset); });
            else bytes = co_await rt::WithContext(io, [data] { return ui::ReadContent(data); });
            if (!bytes) throw std::runtime_error("cannot read " + data);
            co_return Fetched{ std::move(*bytes), DataSource::Disk, {} };
        }
    }

    rt::Task<ImageResult> ImageLoader::Execute(ImageRequest r) {
        ImageResult result;
        if (r.data.empty()) {
            result.error = "no data";
            co_return result;
        }
        const bool memory = r.memoryCachePolicy.read || r.memoryCachePolicy.write;
        const std::string key = r.memoryCacheKey.empty() ? r.data : r.memoryCacheKey;
        if (memory && r.memoryCachePolicy.read) {
            if (const MemoryCache::Value* v = m_Memory.Get(key); v && CachedSizeSatisfies(*v, r)) {
                result.image = v->image;
                result.dataSource = DataSource::MemoryCache;
                result.sampled = v->sampled;
                result.memoryCacheKey = key;
                result.diskCacheKey = v->diskCacheKey;
                co_return result;
            }
        }
        try {
            Fetched fetched = co_await FetchData(this, r);
            auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::move(fetched.bytes));
            Decoded decoded = co_await rt::WithContext(rt::TaskDispatchers::Platform().Default(), [bytes, r] {
                Decoded d;
                bool sampled = false;
                d.image = gfx::ImageBitmap::Decode(*bytes, [&](int w, int h) { return DecodeSize(w, h, r, &sampled); });
                d.sampled = sampled;
                return d;
            });
            if (!decoded.image) throw std::runtime_error("cannot decode the image");
            result.image = decoded.image;
            result.sampled = decoded.sampled;
            result.dataSource = fetched.source;
            result.diskCacheKey = fetched.diskCacheKey;
        } catch (const std::exception& e) {
            result.error = e.what();
            co_return result;
        }
        if (memory && r.memoryCachePolicy.write) {
            m_Memory.Set(key, { result.image, result.sampled, result.diskCacheKey });
            if (m_Memory.Get(key)) result.memoryCacheKey = key;
        }
        co_return result;
    }

}
