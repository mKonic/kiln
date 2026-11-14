#pragma once

// A media::Player's video and a camera's live preview, each in an AndroidExternalSurface sized to
// the picture's aspect ratio (media3 PlayerSurface, CameraX PreviewView's SurfaceView mode).
//
//   auto& player = c.RememberNew<media::Player>({}, {});
//   VideoSurface(FillMax(Modifier{}, Axis::Horizontal), player);
//
//   if (auto back = media::FindCamera(media::LensFacing::Back)) CameraPreview(FillMax(Modifier{}, Axis::Both), back->id);
//
// The player must outlive its VideoSurface (remember it above it). Off Android neither shows
// anything.

#include "kiln/foundation/Compose.h"
#include "kiln/media/Camera.h"
#include "kiln/media/Player.h"

#include <string>

namespace kiln::foundation {

    // keepAspectRatio: the element takes the video's aspect ratio (turned by its rotation); else
    // the video is stretched to the element. showSubtitles: the player's subtitles over it
    // (Subtitles, as PlayerView shows them).
    void VideoSurface(const Modifier& modifier, media::Player& player, bool keepAspectRatio = true, bool showSubtitles = true);

    // The camera's preview, upright on the display, at the largest preview size within 1080p.
    // onError: the camera failed or android.permission.CAMERA is not granted.
    void CameraPreview(const Modifier& modifier, const std::string& cameraId, kiln::Function<void(std::string)> onError = {});

}
