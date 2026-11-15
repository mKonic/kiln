#pragma once

// EXIF orientation (ExifInterface's isFlipped / getRotationDegrees) read from a JPEG's APP1
// segment or a WebP's EXIF chunk, and Coil's ExifUtils.reverseTransformations: the decoded
// pixels flipped, then turned clockwise, to stand as the camera saw them. AImageDecoder applies
// it itself on Android; the dev host's decoders use these.

#include <cstdint>
#include <span>
#include <vector>

namespace kiln::gfx {

    struct ExifOrientation {
        bool flipped = false;     // mirrored left to right before the turn
        int rotationDegrees = 0;  // 0, 90, 180, 270 clockwise
        bool Swaps() const { return rotationDegrees == 90 || rotationDegrees == 270; }
        bool operator==(const ExifOrientation&) const = default;
    };
    // The TIFF orientation tag (1-8) as ExifInterface reads it: 2 flip horizontal, 3 rotate 180,
    // 4 flip vertical (flipped, 180), 5 transpose (flipped, 270), 6 rotate 90, 7 transverse
    // (flipped, 90), 8 rotate 270.
    ExifOrientation OrientationFromTag(int tag);
    // The orientation in a JPEG or WebP; none for other formats or without EXIF.
    ExifOrientation ReadExifOrientation(std::span<const std::uint8_t> encoded);
    // RGBA pixels (4 bytes each) with the orientation reversed; width and height swap for 90 / 270.
    std::vector<std::uint8_t> ApplyExifOrientation(const std::vector<std::uint8_t>& rgba, int& width, int& height, ExifOrientation o);

}
