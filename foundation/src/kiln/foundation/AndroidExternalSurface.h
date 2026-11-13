#pragma once

// AndroidExternalSurface / AndroidEmbeddedExternalSurface (foundation
// AndroidExternalSurface.android.kt): a surface the app renders into itself (a video decoder, a
// camera, its own GL or Vulkan), sized and placed like any element.
//   - AndroidExternalSurface: a surface of its own, composited by the system. Behind (the
//     default) and MediaOverlay sit below the content, which leaves a hole over the element;
//     OnTop sits above it. Cheapest for video; content cannot draw over an OnTop surface.
//   - AndroidEmbeddedExternalSurface: frames drawn inside the content (clips, alpha, overlapping
//     content and `transform` apply), at a copy's cost.
// onInit sets the callbacks once: onSurface runs as a task when the surface exists (cancelled
// when it goes), with onChanged / onDestroyed for its size and end. On a host without external
// surfaces (the desktop dev host), onSurface never runs.

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>
#include "kiln/runtime/Task.h"
#include "kiln/ui/ExternalSurface.h"

#include <array>
#include <functional>
#include <optional>

namespace kiln::foundation {

    // The surface (an ANativeWindow* on Android in nativeWindow), valid until destroyed.
    using Surface = ui::PlatformSurface;

    // SurfaceScope: the size and end of the surface onSurface got.
    class SurfaceScope {
    public:
        virtual ~SurfaceScope() = default;
        virtual void OnChanged(kiln::Function<void(Surface, int width, int height)> onChanged) = 0;
        virtual void OnDestroyed(kiln::Function<void(Surface)> onDestroyed) = 0;
    };
    // SurfaceCoroutineScope: a SurfaceScope whose task stops when the surface goes.
    class SurfaceCoroutineScope : public SurfaceScope {
    public:
        virtual bool IsActive() const = 0;
    };

    class AndroidExternalSurfaceScope {
    public:
        virtual ~AndroidExternalSurfaceScope() = default;
        virtual void OnSurface(kiln::Function<rt::Task<>(SurfaceCoroutineScope&, Surface, int width, int height)> onSurface) = 0;
    };

    // AndroidExternalSurfaceZOrder: Behind, MediaOverlay (over Behind surfaces), OnTop.
    enum class AndroidExternalSurfaceZOrder : int { Behind = 0, MediaOverlay = 1, OnTop = 2 };

    // surfaceSize 0x0: the buffer follows the element's size.
    void AndroidExternalSurface(const Modifier& modifier = {}, bool isOpaque = true, ui::IntSize surfaceSize = {},
                                AndroidExternalSurfaceZOrder zOrder = AndroidExternalSurfaceZOrder::Behind, bool isSecure = false,
                                kiln::Function<void(AndroidExternalSurfaceScope&)> onInit = {}, rt::Key key = {});
    namespace detail {
        // An embedded surface on an element whose measure is someone else's (AndroidView): its
        // frames drawn over the element. Call it in composition (it remembers its state).
        Modifier EmbeddedSurface(const Modifier& modifier, bool isOpaque, kiln::Function<void(AndroidExternalSurfaceScope&)> onInit, rt::Key key = {});
    }

    // transform: a 3x3 row-major matrix for the frames within the element (null: identity).
    void AndroidEmbeddedExternalSurface(const Modifier& modifier = {}, bool isOpaque = true, ui::IntSize surfaceSize = {},
                                        std::optional<std::array<float, 9>> transform = std::nullopt,
                                        kiln::Function<void(AndroidExternalSurfaceScope&)> onInit = {}, rt::Key key = {});

}
