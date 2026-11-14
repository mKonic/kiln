#include "kiln/media/Captioning.h"

namespace kiln::media {

    namespace {
        CaptioningHost& Host() {
            static CaptioningHost host;
            return host;
        }
    }

    void SetCaptioningHost(CaptioningHost host) { Host() = std::move(host); }

    std::optional<std::string> CaptioningPreferredLanguage() {
        auto& h = Host();
        if (!h.isEnabled || !h.isEnabled()) return std::nullopt;
        return h.localeTag ? h.localeTag() : std::nullopt;
    }

    // SubtitleView.getUserCaptionStyle / getUserCaptionFontScale: the user's only while captions are on.
    CaptionStyle UserCaptionStyle() {
        auto& h = Host();
        return h.isEnabled && h.isEnabled() && h.userStyle ? h.userStyle() : CaptionStyle{};
    }
    float CaptioningFontScale() {
        auto& h = Host();
        return h.isEnabled && h.isEnabled() && h.fontScale ? h.fontScale() : 1.0f;
    }

}
