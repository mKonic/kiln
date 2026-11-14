// The image loader (coil-core's MemoryCacheServiceTest isCacheValueValid cases, DecodeUtils'
// sizes, LruCache), its fetchers and caches, and AsyncImage in content. Images are a small
// fake format decoded by a test decoder that honours the requested size.
#include "Check.h"

#include "kiln/foundation/AsyncImage.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/image/ImageLoader.h"
#include "kiln/net/Http.h"
#include "kiln/runtime/Composition.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unistd.h>

using namespace kiln;
using namespace kiln::image;

namespace {
    // "KIMG" w h (4 bytes each, little endian) r g b a: an image of one colour.
    std::string FakeImage(int w, int h, std::uint8_t r = 10, std::uint8_t g = 20, std::uint8_t b = 30) {
        std::string s = "KIMG";
        for (int v : { w, h }) for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xff);
        s += char(r); s += char(g); s += char(b); s += char(255);
        return s;
    }
    std::atomic<int> g_Decodes{ 0 };
    std::optional<gfx::DecodedImage> DecodeFake(std::span<const std::uint8_t> b, const kiln::Function<std::pair<int, int>(int, int)>* size) {
        if (b.size() != 16 || std::string_view(reinterpret_cast<const char*>(b.data()), 4) != "KIMG") return std::nullopt;
        auto u32 = [&](int at) { int v = 0; for (int i = 0; i < 4; ++i) v |= int(b[std::size_t(at + i)]) << (8 * i); return v; };
        const int sw = u32(4), sh = u32(8);
        auto [w, h] = size ? (*size)(sw, sh) : std::pair<int, int>{ sw, sh };
        ++g_Decodes;
        gfx::DecodedImage out;
        out.width = w;
        out.height = h;
        out.sourceWidth = sw;
        out.sourceHeight = sh;
        out.rgba.resize(std::size_t(w) * std::size_t(h) * 4);
        for (std::size_t i = 0; i < out.rgba.size(); i += 4) for (int c = 0; c < 4; ++c) out.rgba[i + std::size_t(c)] = b[12 + std::size_t(c)];
        return out;
    }
    struct FakeDecoders {
        FakeDecoders() {
            gfx::SetImageDecoder([](std::span<const std::uint8_t> b) { return DecodeFake(b, nullptr); });
            gfx::SetSizedImageDecoder([](std::span<const std::uint8_t> b, const kiln::Function<std::pair<int, int>(int, int)>& size) { return DecodeFake(b, &size); });
        }
        ~FakeDecoders() {
            gfx::SetImageDecoder({});
            gfx::SetSizedImageDecoder({});
        }
    };

    std::string Base64(std::string_view s) {
        static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        unsigned acc = 0;
        int bits = 0;
        for (unsigned char c : s) {
            acc = (acc << 8) | c;
            bits += 8;
            while (bits >= 6) { bits -= 6; out += t[(acc >> bits) & 63]; }
        }
        if (bits) out += t[(acc << (6 - bits)) & 63];
        while (out.size() % 4) out += '=';
        return out;
    }
    std::string TempDir() {
        char tmpl[] = "/tmp/kiln-images-XXXXXX";
        return mkdtemp(tmpl) ? std::string(tmpl) : std::string("/tmp");
    }
    void RemoveTree(const std::string& dir) {
        if (dir.rfind("/tmp/kiln-images-", 0) != 0) return;
        std::system(("rm -rf '" + dir + "'").c_str());
    }

    // Answers by URL from a table; writes responseFile as the platform engines do.
    struct FakeHttp final : net::HttpEngine {
        std::map<std::string, std::pair<int, std::string>> pages;
        std::atomic<int> calls{ 0 };
        net::HttpResponse Execute(const net::HttpRequest& request, net::HttpCall&) override {
            ++calls;
            auto it = pages.find(request.url);
            if (it == pages.end()) throw net::HttpError(net::HttpError::Kind::UnknownHost, "no such host");
            net::HttpResponse r;
            r.status = it->second.first;
            r.url = request.url;
            if (!request.responseFile.empty()) {
                FILE* f = std::fopen(request.responseFile.c_str(), "wb");
                std::fwrite(it->second.second.data(), 1, it->second.second.size(), f);
                std::fclose(f);
            } else {
                r.body = it->second.second;
            }
            return r;
        }
    };

    ImageResult Load(ImageLoader& loader, ImageRequest request) {
        auto out = std::make_shared<ImageResult>();
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), [](ImageLoader& l, ImageRequest r, std::shared_ptr<ImageResult> o) -> rt::Task<> {
            *o = co_await l.Execute(std::move(r));
        }(loader, std::move(request), out));
        for (int i = 0; i < 3000 && !job.Done(); ++i) {
            rt::Dispatcher::Main().RunPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(job.Done());
        return *out;
    }

    bool Valid(int cachedW, int cachedH, bool sampled, int w, int h, Scale scale, Precision precision) {
        MemoryCache::Value v;
        std::vector<std::uint8_t> px(std::size_t(cachedW) * std::size_t(cachedH) * 4, 255);
        v.image = gfx::ImageBitmap::FromPixels(cachedW, cachedH, px);
        v.sampled = sampled;
        ImageRequest r;
        r.width = w;
        r.height = h;
        r.scale = scale;
        r.precision = precision;
        return CachedSizeSatisfies(v, r);
    }
}

TEST(ImageCachedValueValidityFollowsCoil) {
    // isCacheValueValid - fill / fit (a 100 x 100 cached bitmap, inexact).
    CHECK(!Valid(100, 100, true, 200, 200, Scale::Fill, Precision::Inexact));
    CHECK(!Valid(100, 100, true, 150, 50, Scale::Fill, Precision::Inexact));
    CHECK(Valid(100, 100, true, 100, 100, Scale::Fill, Precision::Inexact));
    CHECK(Valid(100, 100, true, 50, 100, Scale::Fill, Precision::Inexact));
    CHECK(Valid(100, 100, true, 50, 50, Scale::Fill, Precision::Inexact));
    CHECK(Valid(400, 200, true, 400, 200, Scale::Fill, Precision::Inexact));
    CHECK(!Valid(100, 100, true, 200, 200, Scale::Fit, Precision::Inexact));
    CHECK(Valid(100, 100, true, 150, 50, Scale::Fit, Precision::Inexact));
    CHECK(Valid(100, 100, true, 100, 100, Scale::Fit, Precision::Inexact));
    CHECK(Valid(100, 100, true, 50, 100, Scale::Fit, Precision::Inexact));
    CHECK(Valid(100, 100, true, 50, 50, Scale::Fit, Precision::Inexact));
    CHECK(!Valid(200, 400, true, 400, 800, Scale::Fit, Precision::Inexact));
    // small not sampled cached drawable is valid
    CHECK(Valid(100, 100, false, 200, 200, Scale::Fill, Precision::Inexact));
    // exact precision
    CHECK(!Valid(100, 100, true, 50, 50, Scale::Fill, Precision::Exact));
    CHECK(!Valid(100, 100, true, 50, 50, Scale::Fit, Precision::Exact));
    CHECK(Valid(100, 100, true, 100, 50, Scale::Fill, Precision::Exact));
    CHECK(!Valid(100, 100, true, 100, 50, Scale::Fit, Precision::Exact));
    CHECK(Valid(100, 100, true, 100, 100, Scale::Fill, Precision::Exact));
    CHECK(Valid(100, 100, true, 100, 100, Scale::Fit, Precision::Exact));
}

TEST(ImageDecodeSizeFollowsDecodeUtils) {
    auto size = [](int sw, int sh, int w, int h, Scale scale, Precision p, bool* sampled = nullptr) {
        ImageRequest r;
        r.width = w;
        r.height = h;
        r.scale = scale;
        r.precision = p;
        return DecodeSize(sw, sh, r, sampled);
    };
    bool sampled = false;
    CHECK(size(1000, 500, 100, 100, Scale::Fit, Precision::Exact, &sampled) == std::pair(100, 50) && sampled);
    CHECK(size(1000, 500, 100, 100, Scale::Fill, Precision::Exact) == std::pair(200, 100));
    CHECK(size(1000, 500, 100, 100, Scale::Fit, Precision::Inexact) == std::pair(100, 50));
    // Larger than the source: scaled up only for EXACT.
    CHECK(size(50, 50, 100, 100, Scale::Fit, Precision::Exact, &sampled) == std::pair(100, 100) && !sampled);
    CHECK(size(50, 50, 100, 100, Scale::Fit, Precision::Inexact) == std::pair(50, 50));
    // One side undefined.
    CHECK(size(1000, 500, 100, 0, Scale::Fit, Precision::Exact) == std::pair(100, 50));
    CHECK(size(1000, 500, 100, 0, Scale::Fill, Precision::Exact) == std::pair(100, 50));
    CHECK(size(1000, 500, 0, 100, Scale::Fill, Precision::Exact) == std::pair(200, 100));
    // The original size, but never past maxBitmapSize.
    CHECK(size(800, 600, 0, 0, Scale::Fit, Precision::Exact) == std::pair(800, 600));
    CHECK(size(8192, 1024, 0, 0, Scale::Fit, Precision::Exact, &sampled) == std::pair(4096, 512) && sampled);
}

TEST(ImageBitmapsResampleAndDecodeAtASize) {
    // 4 x 2: left half white, right half black; halved, each output pixel averages its block.
    std::vector<std::uint8_t> px;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) for (int c = 0; c < 4; ++c) px.push_back(c == 3 ? 255 : (x < 2 ? 255 : 0));
    auto b = gfx::ImageBitmap::FromPixels(4, 2, px);
    auto half = b.Scaled(2, 1);
    CHECK(half.Width() == 2 && half.Height() == 1 && half.Pixels()[0] == 255 && half.Pixels()[4] == 0);
    auto quarter = b.Scaled(1, 1);
    CHECK(quarter.Pixels()[0] == 128 && quarter.Pixels()[3] == 255);
    auto up = half.Scaled(4, 2);
    CHECK(up.Width() == 4 && up.Pixels()[0] == 255 && up.Pixels()[12] == 0);
    // Without a sized decoder: decoded whole, then resampled.
    gfx::SetImageDecoder([](std::span<const std::uint8_t> bytes) { return DecodeFake(bytes, nullptr); });
    const std::string data = FakeImage(40, 20);
    std::pair<int, int> source;
    auto d = gfx::ImageBitmap::Decode(std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()),
                                      [](int w, int h) { return std::pair(w / 4, h / 4); }, &source);
    CHECK(d.Width() == 10 && d.Height() == 5 && source == std::pair(40, 20) && d.Pixels()[0] == 10);
    gfx::SetImageDecoder({});
}

TEST(ImageMemoryCacheIsAnLruByBytes) {
    MemoryCache cache(3 * 400);   // three 10 x 10 images
    auto image = [] { std::vector<std::uint8_t> px(400, 1); return gfx::ImageBitmap::FromPixels(10, 10, px); };
    cache.Set("a", { image() });
    cache.Set("b", { image() });
    cache.Set("c", { image() });
    CHECK(cache.Size() == 1200 && cache.Count() == 3);
    CHECK(cache.Get("a"));   // a is now the most recent
    cache.Set("d", { image() });
    CHECK(!cache.Get("b") && cache.Get("a") && cache.Get("c") && cache.Get("d"));
    std::vector<std::uint8_t> big(40 * 40 * 4, 1);
    cache.Set("big", { gfx::ImageBitmap::FromPixels(40, 40, big) });   // larger than the cache
    CHECK(!cache.Get("big") && cache.Count() == 3);
    cache.TrimToSize(400);
    CHECK(cache.Count() == 1 && cache.Get("d"));
    ImageLoader loader({ .memoryCacheBytes = 4000, .diskCacheDirectory = TempDir() });
    loader.Memory().Set("x", { image() });
    loader.Memory().Set("y", { image() });
    loader.TrimMemory(15);   // RUNNING_LOW: half
    CHECK(loader.Memory().Count() == 1);
    loader.TrimMemory(40);   // BACKGROUND: all
    CHECK(loader.Memory().Count() == 0);
    RemoveTree(loader.Disk().Directory());
}

TEST(ImageDiskCacheDropsTheLeastRecentlyUsed) {
    const std::string dir = TempDir();
    DiskCache disk(dir + "/cache", 100);
    auto put = [&](const std::string& key) {
        const std::string edit = disk.EditFile(key);
        FILE* f = std::fopen(edit.c_str(), "wb");
        std::fwrite(std::string(40, 'x').data(), 1, 40, f);
        std::fclose(f);
        return disk.Commit(key, edit);
    };
    CHECK(!put("a").empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!put("b").empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!disk.Get("a").empty());   // a used after b
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!put("c").empty());        // 120 bytes: the least recently used (b) goes
    CHECK(disk.Get("b").empty() && !disk.Get("a").empty() && !disk.Get("c").empty() && disk.Size() == 80);
    const std::string aborted = disk.EditFile("d");
    if (FILE* f = std::fopen(aborted.c_str(), "wb")) std::fclose(f);
    disk.Abort(aborted);
    CHECK(disk.Get("d").empty() && ::access(aborted.c_str(), F_OK) != 0);
    CHECK(disk.Remove("a") && disk.Get("a").empty());
    disk.Clear();
    CHECK(disk.Size() == 0);
    RemoveTree(dir);
}

TEST(ImageLoaderLoadsFromEachSourceAndCaches) {
    FakeDecoders decoders;
    const std::string dir = TempDir();
    ImageLoader loader({ .memoryCacheBytes = 1 << 20, .diskCacheDirectory = dir + "/images" });
    auto http = std::make_shared<FakeHttp>();
    http->pages["https://example.com/a.img"] = { 200, FakeImage(400, 200) };
    http->pages["https://example.com/missing.img"] = { 404, "nope" };
    auto real = net::GetHttpEngine();
    net::SetHttpEngine(http);
    const int decodesBefore = g_Decodes;

    // Network, at the requested size; written to the disk cache.
    ImageResult a = Load(loader, { .data = "https://example.com/a.img", .width = 100, .height = 100 });
    CHECK(a && a.image.Width() == 100 && a.image.Height() == 50 && a.sampled && a.dataSource == DataSource::Network);
    CHECK(a.memoryCacheKey == "https://example.com/a.img" && a.diskCacheKey == "https://example.com/a.img" && http->calls == 1);
    // Again: from memory, no decode.
    ImageResult again = Load(loader, { .data = "https://example.com/a.img", .width = 100, .height = 100 });
    CHECK(again.dataSource == DataSource::MemoryCache && again.image == a.image && g_Decodes == decodesBefore + 1);
    // Larger than the sampled copy in memory: decoded again, from the disk cache.
    ImageResult larger = Load(loader, { .data = "https://example.com/a.img", .width = 200, .height = 200 });
    CHECK(larger.image.Width() == 200 && larger.dataSource == DataSource::Disk && http->calls == 1);
    // A failure status is an error, and nothing is cached.
    ImageResult missing = Load(loader, { .data = "https://example.com/missing.img" });
    CHECK(!missing && missing.error == "HTTP 404" && loader.Disk().Get("https://example.com/missing.img").empty());
    ImageResult unknown = Load(loader, { .data = "https://example.com/other.img" });
    CHECK(!unknown && unknown.error == "no such host");
    // Offline (network not allowed) and not in the disk cache.
    ImageResult offline = Load(loader, { .data = "https://example.com/offline.img", .networkCachePolicy = CachePolicy::Disabled() });
    CHECK(!offline && http->calls == 3);
    // Memory cache off: decoded every time, not kept.
    loader.Memory().Clear();
    ImageResult noMemory = Load(loader, { .data = "https://example.com/a.img", .memoryCachePolicy = CachePolicy::Disabled() });
    CHECK(noMemory && noMemory.image.Width() == 400 && noMemory.memoryCacheKey.empty() && loader.Memory().Count() == 0);

    // data: URIs, files and file:// URIs.
    const std::string image = FakeImage(30, 60, 1, 2, 3);
    ImageResult data = Load(loader, { .data = "data:image/x-kimg;base64," + Base64(image) });
    CHECK(data && data.image.Width() == 30 && data.image.Height() == 60 && data.image.Pixels()[0] == 1);
    const std::string file = dir + "/b.img";
    if (FILE* f = std::fopen(file.c_str(), "wb")) { std::fwrite(image.data(), 1, image.size(), f); std::fclose(f); }
    ImageResult fromFile = Load(loader, { .data = file, .width = 15, .height = 15 });
    CHECK(fromFile && fromFile.image.Width() == 8 && fromFile.image.Height() == 15);
    ImageResult fromUri = Load(loader, { .data = "file://" + file, .width = 10, .height = 10, .precision = Precision::Inexact });
    CHECK(fromUri && fromUri.image.Height() == 10);
    // Bytes that are not an image, a missing file, no data.
    if (FILE* f = std::fopen((dir + "/bad.img").c_str(), "wb")) { std::fputs("garbage", f); std::fclose(f); }
    CHECK(Load(loader, { .data = dir + "/bad.img" }).error == "cannot decode the image");
    CHECK(!Load(loader, { .data = dir + "/none.img" }));
    CHECK(Load(loader, {}).error == "no data");

    net::SetHttpEngine(real);
    RemoveTree(dir);
}

TEST(AsyncImageLoadsAtTheSizeItIsGiven) {
    FakeDecoders decoders;
    const std::string dir = TempDir();
    ImageLoader loader({ .memoryCacheBytes = 1 << 20, .diskCacheDirectory = dir + "/images" });
    const std::string data = "data:image/x-kimg;base64," + Base64(FakeImage(400, 200));
    std::vector<foundation::AsyncImageState::Kind> states;
    foundation::AsyncImageState last;
    rt::State<std::string> model(data);
    auto placeholder = std::make_shared<foundation::ColorPainter>(ui::Color(0xFF808080));
    auto error = std::make_shared<foundation::ColorPainter>(ui::Color(0xFFFF0000));
    {
        ui::Owner owner;
        owner.SetDensity({ 1, 1 });
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        composition.SetContent([&] {
            foundation::Box(ui::Modifier{}, ui::Alignment::TopStart(), false, [&] {
                foundation::AsyncImage(model.Get(), "Cover", foundation::SizeMod(ui::Modifier{}, ui::Dp(100), ui::Dp(100)), {
                    .placeholder = placeholder,
                    .error = error,
                    .onState = [&](const foundation::AsyncImageState& s) { states.push_back(s.kind); last = s; },
                    .imageLoader = &loader,
                });
            });
        });
        auto settle = [&] {
            for (int i = 0; i < 400; ++i) {
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 400, 400 });
                rt::Dispatcher::Main().RunPending();
                rt::Observation::ApplyChanges();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!states.empty() && states.back() != foundation::AsyncImageState::Kind::Loading) break;
            }
            rt::Composition::RecomposeAll();
            owner.MeasureAndLayout({ 400, 400 });
        };
        settle();
        // Loading with the placeholder, then the image at the 100 x 100 px it is laid out in.
        CHECK(states.size() == 2 && states[0] == foundation::AsyncImageState::Kind::Loading && states[1] == foundation::AsyncImageState::Kind::Success);
        CHECK(last.result.image.Width() == 100 && last.result.image.Height() == 50 && last.painter);
        CHECK(last.painter && last.painter->IntrinsicSize() == std::optional(ui::Size{ 100, 50 }));
        // A model that does not load: the error painter.
        states.clear();
        model.Set("data:image/x-kimg;base64,AAAA");
        settle();
        CHECK(states.size() == 2 && states[1] == foundation::AsyncImageState::Kind::Error && last.painter == error);
        // No model: the fallback, or the error painter without one.
        states.clear();
        model.Set("");
        settle();
        CHECK(states.size() == 1 && states[0] == foundation::AsyncImageState::Kind::Error && last.painter == error);
    }
    RemoveTree(dir);
}
