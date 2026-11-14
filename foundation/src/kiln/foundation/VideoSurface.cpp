#include "kiln/foundation/VideoSurface.h"

#include "kiln/foundation/AndroidExternalSurface.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/Configuration.h"

#include <memory>

namespace kiln::foundation {

    void VideoSurface(const Modifier& modifier, media::Player& player, bool keepAspectRatio) {
        const media::PlayerState& s = rt::CollectAsState(player.StateFlow()).Get();
        Modifier m = modifier;
        if (keepAspectRatio && s.videoSize.width > 0 && s.videoSize.height > 0) {
            // PlayerView: the shown size with its pixel aspect ratio.
            m = AspectRatio(m, float(s.videoSize.width) * s.videoSize.pixelWidthHeightRatio / float(s.videoSize.height));
        } else if (keepAspectRatio && s.videoWidth > 0 && s.videoHeight > 0) {
            const bool turned = s.rotationDegrees % 180 != 0;
            m = AspectRatio(m, turned ? float(s.videoHeight) / float(s.videoWidth) : float(s.videoWidth) / float(s.videoHeight));
        }
        media::Player* p = &player;
        AndroidExternalSurface(m, true, {}, AndroidExternalSurfaceZOrder::Behind, false, [p](AndroidExternalSurfaceScope& scope) {
            scope.OnSurface([p](SurfaceCoroutineScope& surface, Surface window, int, int) -> rt::Task<> {
                p->SetSurface(window);
                // The decoder lets go of the window before it goes.
                surface.OnDestroyed([p](Surface) { p->SetSurface({}); });
                co_return;
            });
        });
    }

    void CameraPreview(const Modifier& modifier, const std::string& cameraId, kiln::Function<void(std::string)> onError) {
        auto& c = *rt::Composition::Current();
        const std::vector<media::CameraInfo>& cameras = c.Remember<std::vector<media::CameraInfo>>([] { return media::Cameras(); });
        const media::CameraInfo* info = nullptr;
        for (const auto& cam : cameras) if (cam.id == cameraId) info = &cam;
        if (!info) {
            Box(modifier);
            return;
        }
        const ui::IntSize size = media::ChoosePreviewSize(info->previewSizes);
        const int rotation = media::PreviewRotation(*info, c.Read(ui::LocalConfiguration).displayRotation);
        // The frames come in the sensor's orientation (an image reader takes no transform from
        // the camera) and are turned here, as CameraX turns a TextureView's.
        auto& elementSize = RememberState(ui::IntSize{});
        Modifier m = modifier;
        if (size.width > 0 && size.height > 0)
            m = AspectRatio(m, rotation % 180 != 0 ? float(size.height) / float(size.width) : float(size.width) / float(size.height));
        m = OnSizeChanged(m, [&elementSize](ui::IntSize s) { elementSize.Set(s); });
        std::optional<std::array<float, 9>> transform;
        const ui::IntSize e = elementSize.Get();
        if (e.width > 0 && e.height > 0)
            transform = media::PreviewTransform(rotation, info->facing == media::LensFacing::Front, float(e.width), float(e.height));
        auto handler = std::make_shared<kiln::Function<void(std::string)>>(std::move(onError));
        const std::string id = cameraId;
        AndroidEmbeddedExternalSurface(m, true, size, transform, [id, size, handler](AndroidExternalSurfaceScope& scope) {
            scope.OnSurface([id, size, handler](SurfaceCoroutineScope& surface, Surface window, int, int) -> rt::Task<> {
                const media::CameraHost& host = media::GetCameraHost();
                if (!host.startPreview) co_return;
                std::uint64_t preview = host.startPreview(id, window, size, [handler](std::string message) {
                    if (*handler) (*handler)(std::move(message));
                });
                if (preview) surface.OnDestroyed([preview](Surface) { media::GetCameraHost().stopPreview(preview); });
                co_return;
            });
        }, rt::Key(std::string_view(id)));
    }

}
