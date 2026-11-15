#pragma once

// AsyncImage (coil-compose: AsyncImage.kt, AsyncImagePainter.kt, ConstraintsSizeResolver.kt):
// an image loaded in the background by an image::ImageLoader, at the size its layout gives it.
// While it loads it shows the placeholder, on failure the error painter.
//
//   AsyncImage("https://example.com/cover.jpg", "Cover", SizeMod(Modifier{}, 96_dp, 96_dp), {
//       .placeholder = std::make_shared<ColorPainter>(ui::Color(0xFFE0E0E0)),
//       .contentScale = ContentScale::Crop(),
//   });

#include "kiln/foundation/Image.h"
#include "kiln/image/ImageLoader.h"
#include "kiln/runtime/State.h"

#include <memory>
#include <optional>
#include <string>

namespace kiln::foundation {

    // CrossfadePainter: `start` fading out (unless fadeStart is false) as `end` fades in over
    // the duration, each drawn centred at its content-scaled size; it redraws itself until done.
    // Time runs from the first draw, on the frame clock.
    class CrossfadePainter final : public Painter {
    public:
        CrossfadePainter(std::shared_ptr<Painter> start, std::shared_ptr<Painter> end, ContentScale contentScale = ContentScale::Fit(),
                         int durationMillis = 200, bool fadeStart = true, bool preferExactIntrinsicSize = false);
        std::optional<Size> IntrinsicSize() const override { return m_Intrinsic; }
        void Draw(DrawScope& scope, Size size, float alpha, const gfx::ColorFilter* colorFilter) override;
        const std::shared_ptr<Painter>& Start() const { return m_Start; }
        const std::shared_ptr<Painter>& End() const { return m_End; }

    private:
        void DrawPainter(DrawScope& scope, Painter* painter, Size size, float alpha, const gfx::ColorFilter* colorFilter);
        std::shared_ptr<Painter> m_Start, m_End;
        ContentScale m_ContentScale;
        int m_Duration;
        bool m_FadeStart;
        std::optional<Size> m_Intrinsic;
        std::optional<std::int64_t> m_StartNanos;
        bool m_Done = false;
        rt::State<int> m_Tick{ 0 };
        std::shared_ptr<int> m_Alive = std::make_shared<int>(0);
    };

    // AsyncImagePainter.State.
    struct AsyncImageState {
        enum class Kind { Empty, Loading, Success, Error };
        Kind kind = Kind::Empty;
        std::shared_ptr<Painter> painter;   // what is drawn now (may be null)
        image::ImageResult result;          // Success and Error
    };

    struct AsyncImageOptions {
        // Shown while loading, when the load fails, and when the model is empty (the error
        // painter when there is no fallback).
        std::shared_ptr<Painter> placeholder, error, fallback;
        // Told of each state: Loading, then Success or Error.
        kiln::Function<void(const AsyncImageState&)> onState;
        Alignment alignment = Alignment::Center();
        ContentScale contentScale = ContentScale::Fit();
        float alpha = 1;
        std::optional<gfx::ColorFilter> colorFilter;
        gfx::FilterQuality filterQuality = gfx::FilterQuality::Low;
        // ImageRequest.crossfade: a successful load not from the memory cache crossfades from the
        // placeholder over this many ms (crossfade(true): 200); 0: none.
        int crossfadeMillis = 0;
        // Null: ImageLoader::Default().
        image::ImageLoader* imageLoader = nullptr;
    };

    // AsyncImagePainter: draws the request's current state (its painter), loading it once it
    // knows the size: the request's, the one SetSize gives (constraints), or the size it is first
    // drawn at (DrawScopeSizeResolver). A memory cache hit is there from the start.
    class AsyncImagePainter final : public Painter {
    public:
        struct Impl;
        explicit AsyncImagePainter(std::shared_ptr<Impl> impl) : m_Impl(std::move(impl)) {}
        // The state now (read in content or drawing, it is observed).
        const AsyncImageState& State() const;
        std::optional<Size> IntrinsicSize() const override;
        void Draw(DrawScope& scope, Size size, float alpha, const gfx::ColorFilter* colorFilter) override;
        // ConstraintsSizeResolver.setConstraints: the size in px to load at (0: an unbounded
        // side), when the request has none; the first one counts.
        void SetSize(int width, int height);
        const std::shared_ptr<Impl>& GetImpl() const { return m_Impl; }

    private:
        std::shared_ptr<Impl> m_Impl;
    };
    // rememberAsyncImagePainter: remembered for the request (a new one when it changes).
    std::shared_ptr<AsyncImagePainter> RememberAsyncImagePainter(const image::ImageRequest& request, const AsyncImageOptions& options = {});
    std::shared_ptr<AsyncImagePainter> RememberAsyncImagePainter(const std::string& model, const AsyncImageOptions& options = {});

    // SubcomposeAsyncImageScope: what the content draws with (SubcomposeAsyncImageContent).
    struct SubcomposeAsyncImageScope {
        std::shared_ptr<AsyncImagePainter> painter;
        AsyncImageState state;
        std::optional<std::string> contentDescription;
        Alignment alignment = Alignment::Center();
        ContentScale contentScale = ContentScale::Fit();
        float alpha = 1;
        std::optional<gfx::ColorFilter> colorFilter;
    };
    // The image itself, as AsyncImage draws it (an Image filling the min constraints).
    void SubcomposeAsyncImageContent(const SubcomposeAsyncImageScope& scope, const Modifier& modifier = {});
    // SubcomposeAsyncImage: content composed for each state, in a box the size of its constraints
    // (they size the request when it has none). The slots' form: a slot per state, the image when
    // a state has none.
    void SubcomposeAsyncImage(const image::ImageRequest& request, const std::optional<std::string>& contentDescription, const Modifier& modifier,
                              const AsyncImageOptions& options, kiln::Function<void(const SubcomposeAsyncImageScope&)> content);
    struct SubcomposeAsyncImageSlots {
        kiln::Function<void(const SubcomposeAsyncImageScope&)> loading, success, error;
    };
    void SubcomposeAsyncImage(const std::string& model, const std::optional<std::string>& contentDescription, const Modifier& modifier,
                              const AsyncImageOptions& options, SubcomposeAsyncImageSlots slots);

    // `model` is ImageRequest::data: a URL, content or file URI, path, asset:///path or data: URI.
    void AsyncImage(const std::string& model, const std::optional<std::string>& contentDescription, const Modifier& modifier = {},
                    const AsyncImageOptions& options = {});
    // A request of its own (headers, cache policies, keys). Without a width or height it is
    // loaded at the size of the layout's constraints (unbounded sides are left undefined), with
    // ContentScale::None at the image's own size; the scale follows the content scale.
    void AsyncImage(const image::ImageRequest& request, const std::optional<std::string>& contentDescription, const Modifier& modifier = {},
                    const AsyncImageOptions& options = {});

}
