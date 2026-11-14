#pragma once

// Loading images in the background, in Coil's model (coil-core: ImageLoader, ImageRequest,
// EngineInterceptor, MemoryCacheService, DecodeUtils, NetworkFetcher): a request names the data
// and the size it is shown at; the loader looks in its memory cache, then fetches the bytes
// (network through its disk cache, content URIs, files, assets, data: URIs), decodes them at
// the requested size off the main thread and keeps the result in memory.
//
//   image::ImageResult r = co_await image::ImageLoader::Default().Execute({ .data = url, .width = 300, .height = 300 });
//   if (r.image) ...
//
// AsyncImage (kiln/foundation/AsyncImage.h) does this for content.

#include "kiln/graphics/Image.h"
#include "kiln/net/Http.h"
#include "kiln/runtime/Task.h"

#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace kiln::image {

    // How the image is fitted into the requested size (Coil's Scale): FIT keeps it inside both
    // sides, FILL covers both.
    enum class Scale { Fit, Fill };
    // EXACT decodes at the requested size (scaling up too); INEXACT may return a larger image
    // and never scales up (what AsyncImage uses: it is scaled when drawn).
    enum class Precision { Exact, Inexact };
    struct CachePolicy {
        bool read = true, write = true;
        static constexpr CachePolicy Enabled() { return { true, true }; }
        static constexpr CachePolicy ReadOnly() { return { true, false }; }
        static constexpr CachePolicy WriteOnly() { return { false, true }; }
        static constexpr CachePolicy Disabled() { return { false, false }; }
        bool operator==(const CachePolicy&) const = default;
    };
    // Where the image came from (Coil's DataSource).
    enum class DataSource { MemoryCache, Memory, Disk, Network };

    struct ImageRequest {
        // https:// and http:// URLs, content:// and file:// URIs, absolute paths, app assets
        // (asset:///path or file:///android_asset/path) and data: URIs (base64).
        std::string data;
        // The size in px it is shown at; 0 leaves that side undefined. Both 0: the image's
        // own size (still limited by maxWidth / maxHeight).
        int width = 0, height = 0;
        Scale scale = Scale::Fit;
        Precision precision = Precision::Exact;
        // No decoded bitmap is larger than this (Coil's maxBitmapSize, 4096 x 4096).
        int maxWidth = 4096, maxHeight = 4096;
        // Sent with network requests.
        net::Headers headers;
        CachePolicy memoryCachePolicy, diskCachePolicy, networkCachePolicy;
        // The keys in the caches; empty: from data.
        std::string memoryCacheKey, diskCacheKey;
        bool operator==(const ImageRequest&) const = default;
    };

    struct ImageResult {
        gfx::ImageBitmap image;            // empty on failure
        std::string error;                 // why it failed
        DataSource dataSource = DataSource::Memory;
        bool sampled = false;              // decoded smaller than the source
        std::string memoryCacheKey;        // set when it is in the memory cache
        std::string diskCacheKey;          // set when the bytes came through the disk cache
        explicit operator bool() const { return bool(image); }
    };

    // A strong LRU cache of decoded images by key, bounded by pixel bytes. Main thread only.
    class MemoryCache {
    public:
        struct Value { gfx::ImageBitmap image; bool sampled = false; std::string diskCacheKey; };
        explicit MemoryCache(std::size_t maxBytes) : m_Max(maxBytes) {}
        const Value* Get(const std::string& key);
        void Set(const std::string& key, Value value);
        bool Remove(const std::string& key);
        void Clear();
        // Drops the least recently used entries until at most `bytes` are kept.
        void TrimToSize(std::size_t bytes);
        std::size_t Size() const { return m_Size; }
        std::size_t MaxSize() const { return m_Max; }
        void SetMaxSize(std::size_t bytes) { m_Max = bytes; TrimToSize(bytes); }
        std::size_t Count() const { return m_Map.size(); }

    private:
        using Entry = std::pair<std::string, Value>;
        std::list<Entry> m_Order;   // most recent first
        std::unordered_map<std::string, std::list<Entry>::iterator> m_Map;
        std::size_t m_Size = 0, m_Max;
    };

    // Encoded bytes in files under a directory, least recently used dropped beyond maxBytes.
    // Any thread.
    class DiskCache {
    public:
        DiskCache(std::string directory, std::int64_t maxBytes);
        // The file holding the key's bytes (its use time updated), or empty when absent.
        std::string Get(const std::string& key);
        // A file to write the key's bytes to, then Commit or Abort it.
        std::string EditFile(const std::string& key);
        // Moves the written file into place; returns the entry's file.
        std::string Commit(const std::string& key, const std::string& editFile);
        void Abort(const std::string& editFile);
        bool Remove(const std::string& key);
        void Clear();
        std::int64_t Size();
        const std::string& Directory() const { return m_Dir; }
        std::int64_t MaxSize() const { return m_Max; }

    private:
        std::string PathOf(const std::string& key) const;
        void Trim();
        std::string m_Dir;
        std::int64_t m_Max;
        std::mutex m_Mutex;
        std::uint64_t m_NextEdit = 0;
    };

    class ImageLoader {
    public:
        struct Options {
            // Bytes of decoded images kept in memory. 0: Coil's default, a fifth of the app's
            // memory class on Android (ActivityManager), 15% of 512 MB elsewhere.
            std::size_t memoryCacheBytes = 0;
            // The disk cache's directory and size. Empty: <cache dir>/image_cache; 0: 2% of
            // the free space, between 10 and 250 MB.
            std::string diskCacheDirectory;
            std::int64_t diskCacheBytes = 0;
        };
        ImageLoader();
        explicit ImageLoader(Options options);
        // The process's loader (what AsyncImage uses unless given another).
        static ImageLoader& Default();

        // Loads the request. Runs on the caller's dispatcher between background steps; the
        // result arrives there. A failure is a result with an error, not an exception.
        rt::Task<ImageResult> Execute(ImageRequest request);

        MemoryCache& Memory() { return m_Memory; }
        // Created on first use.
        DiskCache& Disk();
        // ComponentCallbacks2.onTrimMemory: clears memory at the background levels and above
        // (TRIM_MEMORY_BACKGROUND, 40), halves it at RUNNING_LOW (10) and above.
        void TrimMemory(int level);

    private:
        Options m_Options;
        MemoryCache m_Memory;
        std::unique_ptr<DiskCache> m_Disk;
        std::mutex m_DiskMutex;
    };

    // The memory budget the platform gives the app (Android: memoryClass / largeMemoryClass MB,
    // and whether it is a low-RAM device); set by the host before the first ImageLoader.
    void SetMemoryClass(std::int64_t bytes, bool lowRam);

    // DecodeUtils: the size an image of src is decoded at for the request (Coil's
    // computeDstSize and computeSizeMultiplier); `sampled` when that is smaller than src.
    std::pair<int, int> DecodeSize(int srcWidth, int srcHeight, const ImageRequest& request, bool* sampled = nullptr);
    // MemoryCacheService.isCacheValueValidForSize.
    bool CachedSizeSatisfies(const MemoryCache::Value& value, const ImageRequest& request);

}
