#include "kiln/ui/PictureInPicture.h"

#include "kiln/runtime/State.h"

namespace kiln::ui {

    namespace {
        PictureInPictureHost& Host() { static auto* h = new PictureInPictureHost; return *h; }
        rt::State<bool>& InPip() { static auto* s = new rt::State<bool>(false); return *s; }
        ScreenOrientation& Requested() { static ScreenOrientation o = ScreenOrientation::Unspecified; return o; }
    }

    bool EnterPictureInPictureMode(const PictureInPictureParams& params) {
        return Host().apply && Host().apply(params, true);
    }
    bool SetPictureInPictureParams(const PictureInPictureParams& params) {
        return Host().apply && Host().apply(params, false);
    }
    bool IsInPictureInPictureMode() { return InPip().Get(); }

    void SetRequestedOrientation(ScreenOrientation orientation) {
        Requested() = orientation;
        if (Host().setOrientation) Host().setOrientation(int(orientation));
    }
    ScreenOrientation RequestedOrientation() {
        return Host().orientation ? ScreenOrientation(Host().orientation()) : Requested();
    }

    void SetPictureInPictureHost(PictureInPictureHost host) { Host() = std::move(host); }
    namespace {
        rt::State<PictureInPictureUiState>& UiState() {
            static auto* s = new rt::State<PictureInPictureUiState>(PictureInPictureUiState{});
            return *s;
        }
    }
    PictureInPictureUiState GetPictureInPictureUiState() { return UiState().Get(); }
    void SetPictureInPictureUiState(PictureInPictureUiState state) {
        if (!(UiState().Peek() == state)) UiState().Set(state);
    }

    void SetInPictureInPictureMode(bool inPip) {
        if (InPip().Peek() != inPip) InPip().Set(inPip);
    }

}
