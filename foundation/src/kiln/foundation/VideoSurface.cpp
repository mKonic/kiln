#include "kiln/foundation/VideoSurface.h"

#include "kiln/foundation/AndroidExternalSurface.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Subtitles.h"
#include "kiln/runtime/Composition.h"
#include "kiln/ui/Configuration.h"

#include <memory>

namespace kiln::foundation {

    void VideoSurface(const Modifier& modifier, media::Player& player, bool keepAspectRatio, bool showSubtitles) {
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
        // PlayerView: the subtitles over the picture, in the same frame.
        Box(m, Alignment::TopStart(), false, [p, showSubtitles] {
            AndroidExternalSurface(MatchParentSize(Modifier{}), true, {}, AndroidExternalSurfaceZOrder::Behind, false, [p](AndroidExternalSurfaceScope& scope) {
                scope.OnSurface([p](SurfaceCoroutineScope& surface, Surface window, int, int) -> rt::Task<> {
                    p->SetSurface(window);
                    // The decoder lets go of the window before it goes.
                    surface.OnDestroyed([p](Surface) { p->SetSurface({}); });
                    co_return;
                });
            });
            if (showSubtitles) Subtitles(MatchParentSize(Modifier{}), *p);
        });
    }

    void CameraPreview(const Modifier& modifier, media::Camera& camera, CameraPreviewOptions options) {
        auto& c = *rt::Composition::Current();
        const media::CameraInfo& info = camera.Info();
        const ui::IntSize size = camera.PreviewSize();
        const int rotation = media::PreviewRotation(info, c.Read(ui::LocalConfiguration).displayRotation);
        const bool mirror = info.facing == media::LensFacing::Front;
        // The frames come in the sensor's orientation (an image reader takes no transform from
        // the camera) and are turned here, as CameraX turns a TextureView's.
        auto& elementSize = RememberState(ui::IntSize{});
        Modifier m = modifier;
        if (size.width > 0 && size.height > 0)
            m = AspectRatio(m, rotation % 180 != 0 ? float(size.height) / float(size.width) : float(size.width) / float(size.height));
        m = OnSizeChanged(m, [&elementSize](ui::IntSize s) { elementSize.Set(s); });
        media::Camera* cam = &camera;
        // CameraController's gestures: a tap focuses and meters there, a pinch zooms (twice as fast).
        if (options.tapToFocus) {
            auto onFocus = std::make_shared<kiln::Function<void(bool)>>(std::move(options.onTapToFocus));
            m = PointerInput(m, rt::Keys{ cam, rotation, mirror }, [cam, rotation, mirror, size, onFocus, &elementSize](PointerInputScope& scope) -> rt::Task<> {
                co_await DetectTapGestures(scope, { .onTap = [cam, rotation, mirror, size, onFocus, &elementSize](Offset tap) {
                    const ui::IntSize e = elementSize.Get();
                    media::MeteringPoint af = media::PreviewMeteringPoint(tap, e, rotation, mirror, size);
                    media::MeteringPoint ae = af;
                    af.size = 1.0f / 6.0f;
                    ae.size = af.size * 1.5f;
                    auto action = media::FocusMeteringAction::Of(af, media::kMeteringAf);
                    action.AddPoint(ae, media::kMeteringAe);
                    cam->StartFocusAndMetering(std::move(action), [onFocus](std::optional<media::FocusMeteringResult> r, std::optional<media::CameraError>) {
                        if (*onFocus) (*onFocus)(r && r->isFocusSuccessful);
                    });
                } });
            });
        }
        if (options.pinchToZoom) {
            m = PointerInput(m, rt::Keys{ cam }, [cam](PointerInputScope& scope) -> rt::Task<> {
                co_await DetectTransformGestures(scope, false, [cam](Offset, Offset, float zoom, float) {
                    if (zoom == 1) return;
                    const media::ZoomState z = cam->State().zoom;
                    const float faster = zoom > 1 ? 1 + (zoom - 1) * 2 : 1 - (1 - zoom) * 2;
                    cam->SetZoomRatio(std::clamp(z.zoomRatio * faster, z.minZoomRatio, z.maxZoomRatio));
                });
            });
        }
        std::optional<std::array<float, 9>> transform;
        const ui::IntSize e = elementSize.Get();
        if (e.width > 0 && e.height > 0) transform = media::PreviewTransform(rotation, mirror, float(e.width), float(e.height));
        AndroidEmbeddedExternalSurface(m, true, size, transform, [cam, size](AndroidExternalSurfaceScope& scope) {
            scope.OnSurface([cam, size](SurfaceCoroutineScope& surface, Surface window, int, int) -> rt::Task<> {
                cam->SetPreviewSurface(window, size);
                surface.OnDestroyed([cam](Surface) { cam->SetPreviewSurface({}, {}); });
                co_return;
            });
        }, rt::Key(std::int64_t(reinterpret_cast<std::intptr_t>(cam))));
    }

    void CameraPreview(const Modifier& modifier, const std::string& cameraId, kiln::Function<void(std::string)> onError) {
        auto& c = *rt::Composition::Current();
        auto& camera = c.RememberNew<media::Camera>(rt::Keys{ cameraId }, {}, cameraId);
        // Errors as they come (the camera failed, or the permission is missing).
        const media::CameraState& s = rt::CollectAsState(camera.StateFlow()).Get();
        auto handler = std::make_shared<kiln::Function<void(std::string)>>(std::move(onError));
        c.LaunchedEffect(rt::Keys{ s.errorMessage }, [handler, message = s.errorMessage]() -> rt::Task<> {
            if (!message.empty() && *handler) (*handler)(message);
            co_return;
        });
        CameraPreview(modifier, camera, { .tapToFocus = false, .pinchToZoom = false });
    }

}
