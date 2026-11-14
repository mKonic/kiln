#pragma once

// The device's cameras and a live preview into a surface (Android: the NDK's ACameraManager, a
// TEMPLATE_PREVIEW repeating request). CameraPreview (kiln/foundation/VideoSurface.h) shows one;
// the app needs android.permission.CAMERA granted first (ui::RequestPermission).

#include "kiln/ui/ExternalSurface.h"
#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kiln::media {

    // CameraCharacteristics.LENS_FACING.
    enum class LensFacing { Front, Back, External };

    struct CameraInfo {
        std::string id;
        LensFacing facing = LensFacing::Back;
        // Clockwise degrees the sensor's image turns to stand upright on the device in its
        // natural orientation (SENSOR_ORIENTATION).
        int sensorOrientation = 0;
        // The sizes a preview stream can have, in the sensor's orientation.
        std::vector<ui::IntSize> previewSizes;
        bool operator==(const CameraInfo&) const = default;
    };

    std::vector<CameraInfo> Cameras();
    std::optional<CameraInfo> FindCamera(LensFacing facing);

    // The preview size for a surface: the largest the camera offers within 1920 x 1080 (the
    // preview limit CameraX keeps), with the aspect ratio nearest `aspect` (width / height in the
    // sensor's orientation; 0: any).
    ui::IntSize ChoosePreviewSize(const std::vector<ui::IntSize>& sizes, float aspect = 0);

    // How far the preview turns to stand upright on the display (CameraX's relative rotation:
    // the sensor's orientation less the display's rotation, plus it for a front camera).
    int PreviewRotation(const CameraInfo& camera, int displayRotation);
    // The matrix (row-major 3x3, element pixels) that turns a sensor-oriented frame, drawn
    // stretched over a w x h element, upright and unstretched (CameraX PreviewTransformation's
    // TextureView correction); `mirror` flips it as a front camera's preview is shown.
    std::array<float, 9> PreviewTransform(int rotationDegrees, bool mirror, float width, float height);

    struct CameraHost {
        kiln::Function<std::vector<CameraInfo>()> cameras;
        // Starts the preview into the surface (its buffers set to `size`); returns a handle, 0 on
        // failure. onError runs on the main thread (a lost camera, a missing permission).
        kiln::Function<std::uint64_t(const std::string& cameraId, ui::PlatformSurface surface, ui::IntSize size,
                                     kiln::Function<void(std::string)> onError)> startPreview;
        // Stops it; the surface is free when this returns.
        kiln::Function<void(std::uint64_t)> stopPreview;
    };
    void SetCameraHost(CameraHost host);
    const CameraHost& GetCameraHost();

}
