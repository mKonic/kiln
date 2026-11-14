#include "kiln/media/Camera.h"

#include <cmath>

namespace kiln::media {

    namespace {
        CameraHost& Host() {
            static CameraHost h;
            return h;
        }
    }

    void SetCameraHost(CameraHost host) { Host() = std::move(host); }
    const CameraHost& GetCameraHost() { return Host(); }

    std::vector<CameraInfo> Cameras() { return Host().cameras ? Host().cameras() : std::vector<CameraInfo>{}; }

    std::optional<CameraInfo> FindCamera(LensFacing facing) {
        for (CameraInfo& c : Cameras()) if (c.facing == facing) return std::move(c);
        return std::nullopt;
    }

    int PreviewRotation(const CameraInfo& camera, int displayRotation) {
        return camera.facing == LensFacing::Front ? (camera.sensorOrientation + displayRotation) % 360
                                                  : (camera.sensorOrientation - displayRotation + 360) % 360;
    }

    std::array<float, 9> PreviewTransform(int rotation, bool mirror, float w, float h) {
        rotation = ((rotation % 360) + 360) % 360;
        // Quarter turns only: exact cosines and sines.
        const float c = rotation == 0 ? 1.0f : rotation == 180 ? -1.0f : 0.0f;
        const float s = rotation == 90 ? 1.0f : rotation == 270 ? -1.0f : 0.0f;
        // The stretch undone: a quarter-turned frame swaps the element's aspect ratio.
        const float sx = rotation % 180 != 0 ? h / w : 1.0f, sy = rotation % 180 != 0 ? w / h : 1.0f;
        float a = c * sx, b = -s * sy, d = s * sx, e = c * sy;
        if (mirror) {
            a = -a;
            b = -b;
        }
        // About the element's centre.
        const float tx = w / 2 - (a * w / 2 + b * h / 2), ty = h / 2 - (d * w / 2 + e * h / 2);
        return { a, b, tx, d, e, ty, 0, 0, 1 };
    }

    ui::IntSize ChoosePreviewSize(const std::vector<ui::IntSize>& sizes, float aspect) {
        ui::IntSize best{};
        float bestAspectError = 1e9f;
        for (const ui::IntSize& s : sizes) {
            const int longSide = std::max(s.width, s.height), shortSide = std::min(s.width, s.height);
            if (s.width <= 0 || s.height <= 0 || longSide > 1920 || shortSide > 1080) continue;
            const float error = aspect > 0 ? std::abs(std::log(float(s.width) / float(s.height) / aspect)) : 0;
            const bool closer = error < bestAspectError - 1e-3f;
            const bool asClose = std::abs(error - bestAspectError) <= 1e-3f;
            if (closer || (asClose && std::int64_t(s.width) * s.height > std::int64_t(best.width) * best.height)) {
                best = s;
                bestAspectError = error;
            }
        }
        return best;
    }

}
