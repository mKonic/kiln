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

#include <memory>
#include <optional>
#include <string>

namespace kiln::foundation {

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
        // Null: ImageLoader::Default().
        image::ImageLoader* imageLoader = nullptr;
    };

    // `model` is ImageRequest::data: a URL, content or file URI, path, asset:///path or data: URI.
    void AsyncImage(const std::string& model, const std::optional<std::string>& contentDescription, const Modifier& modifier = {},
                    const AsyncImageOptions& options = {});
    // A request of its own (headers, cache policies, keys). Without a width or height it is
    // loaded at the size of the layout's constraints (unbounded sides are left undefined), with
    // ContentScale::None at the image's own size; the scale follows the content scale.
    void AsyncImage(const image::ImageRequest& request, const std::optional<std::string>& contentDescription, const Modifier& modifier = {},
                    const AsyncImageOptions& options = {});

}
