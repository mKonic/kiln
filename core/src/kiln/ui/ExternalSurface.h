#pragma once

// External surfaces (AndroidExternalSurface / AndroidEmbeddedExternalSurface's host side): a
// platform surface an app renders into itself (video, camera, its own GL or Vulkan), placed at a
// layout node's bounds. On Android it is an ANativeWindow:
//   - a separate surface (AndroidExternalSurface) is a layer of its own, below kiln's frame
//     (Behind, MediaOverlay above Behind; kiln clears its frame over the bounds so it shows) or
//     above it (OnTop);
//   - an embedded surface's frames are drawn inside kiln's frame like an image, so content,
//     clips and alpha around it apply.
// The host creates the surface when asked and reports its lifecycle; content decides where.

#include "kiln/ui/Geometry.h"

namespace kiln { class AtlasPage; }

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace kiln::ui {

    // A platform surface (ANativeWindow* on Android), valid from created until destroyed.
    struct PlatformSurface {
        void* nativeWindow = nullptr;
        bool operator==(const PlatformSurface&) const = default;
    };

    struct ExternalSurfaceConfig {
        IntRect bounds;                    // window pixels
        IntSize fixedSize;                 // the buffer size; 0x0 follows the bounds
        bool opaque = true;
        int zOrder = 0;                    // 0 Behind, 1 MediaOverlay, 2 OnTop
        bool secure = false;
        bool embedded = false;
        // Embedded: a 3x3 row-major matrix applied to the frames inside the bounds.
        std::optional<std::array<float, 9>> transform;
        bool visible = true;
        bool operator==(const ExternalSurfaceConfig&) const = default;
    };

    // What the surface's owner in content is told.
    class ExternalSurfaceClient {
    public:
        virtual ~ExternalSurfaceClient() = default;
        virtual void OnSurfaceCreated(PlatformSurface surface, int width, int height) = 0;
        virtual void OnSurfaceChanged(PlatformSurface surface, int width, int height) = 0;
        virtual void OnSurfaceDestroyed(PlatformSurface surface) = 0;
        // Embedded: a new frame is there to draw.
        virtual void OnFrameAvailable() {}
    };

    // An embedded surface's latest frame, as the renderer draws it.
    struct ExternalFrame {
        const AtlasPage* page = nullptr;
        int width = 0, height = 0;
    };

    // Host side: creates, places and releases surfaces by id.
    struct ExternalSurfaceHost {
        std::function<void(int id, const ExternalSurfaceConfig&)> create;
        std::function<void(int id, const ExternalSurfaceConfig&)> update;
        std::function<void(int id)> release;
        // Embedded: the frame to draw now (the latest the producer posted).
        std::function<std::optional<ExternalFrame>(int id)> frame;
    };

}
