#pragma once

// Picture-in-picture and the requested screen orientation of the activity (Activity's
// enterPictureInPictureMode, setPictureInPictureParams, isInPictureInPictureMode,
// setRequestedOrientation). The app's activity declares android:supportsPictureInPicture="true"
// (ACTIVITY_ATTRIBUTES in kiln_add_app); without it, or off Android, entering fails.
//
//   if (ui::IsInPictureInPictureMode()) VideoOnly(); else VideoWithControls();
//   ui::SetPictureInPictureParams({ .aspectWidth = 16, .aspectHeight = 9, .autoEnter = true });

#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>

#include <optional>
#include <string>

namespace kiln::ui {

    struct PictureInPictureParams {
        // The window's aspect ratio (Rational); 0: the platform's. Android allows 2.39:1 to
        // 1:2.39 and refuses others.
        int aspectWidth = 0, aspectHeight = 0;
        // The part of the window shown in picture-in-picture, in window pixels: the system
        // animates from it (setSourceRectHint).
        std::optional<IntRect> sourceRectHint;
        // Enter by itself when the user leaves the app (setAutoEnterEnabled).
        bool autoEnter = false;
        // Resize without a cross-fade (setSeamlessResizeEnabled): for video, not for text.
        bool seamlessResize = true;
        std::string title, subtitle;
        bool operator==(const PictureInPictureParams&) const = default;
    };

    // Enters picture-in-picture with the params; false when the activity or device cannot.
    bool EnterPictureInPictureMode(const PictureInPictureParams& params = {});
    // The params for later: the next enter, auto-enter and the window's aspect ratio while in it.
    bool SetPictureInPictureParams(const PictureInPictureParams& params);
    // Whether the activity is in picture-in-picture now. Content that reads it recomposes when
    // it enters or leaves.
    bool IsInPictureInPictureMode();

    // ActivityInfo.SCREEN_ORIENTATION_*.
    enum class ScreenOrientation : int {
        Unspecified = -1, Landscape = 0, Portrait = 1, User = 2, Behind = 3, Sensor = 4, NoSensor = 5,
        SensorLandscape = 6, SensorPortrait = 7, ReverseLandscape = 8, ReversePortrait = 9, FullSensor = 10,
        UserLandscape = 11, UserPortrait = 12, FullUser = 13, Locked = 14,
    };
    // Activity.setRequestedOrientation / getRequestedOrientation.
    void SetRequestedOrientation(ScreenOrientation orientation);
    ScreenOrientation RequestedOrientation();

    // The host.
    struct PictureInPictureHost {
        kiln::Function<bool(const PictureInPictureParams&, bool enter)> apply;
        kiln::Function<void(int)> setOrientation;
        kiln::Function<int()> orientation;
    };
    void SetPictureInPictureHost(PictureInPictureHost host);
    void SetInPictureInPictureMode(bool inPip);

}
