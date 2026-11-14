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

    CaptionStyle UserCaptionStyle() { return Host().userStyle ? Host().userStyle() : CaptionStyle{}; }
    float CaptioningFontScale() { return Host().fontScale ? Host().fontScale() : 1.0f; }

}
