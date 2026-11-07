#pragma once

// The configuration surface: android.content.res.Configuration as LocalConfiguration gives it
// (isSystemInDarkTheme reads its night mode), the 24-hour time setting (DateFormat.is24HourFormat,
// which material3's time picker reads), the platform locale list (Locale.current /
// LocaleList.current), and the host window (LocalWindow) with FLAG_SECURE and SecureFlagPolicy.

#include "kiln/runtime/Composition.h"

#include <mutex>
#include <string>
#include <cstdint>
#include <vector>

namespace kiln::ui {

    // The Configuration fields the host fills (same values and constants as Android's).
    struct Configuration {
        static constexpr int kUiModeTypeMask = 0x0f, kUiModeTypeNormal = 0x01;
        static constexpr int kUiModeNightMask = 0x30, kUiModeNightNo = 0x10, kUiModeNightYes = 0x20;
        static constexpr int kOrientationPortrait = 1, kOrientationLandscape = 2;
        static constexpr int kScreenLayoutRoundMask = 0x300, kScreenLayoutRoundYes = 0x200;
        static constexpr int kColorModeWideColorGamutMask = 0x3, kColorModeWideColorGamutYes = 0x2;
        static constexpr int kColorModeHdrMask = 0xc, kColorModeHdrYes = 0x8;
        static constexpr int kTouchscreenNoTouch = 1, kTouchscreenFinger = 3;
        static constexpr int kKeyboardNoKeys = 1, kKeyboardQwerty = 2, kKeyboard12Key = 3;
        static constexpr int kHardKeyboardHiddenNo = 1, kHardKeyboardHiddenYes = 2;
        static constexpr int kNavigationNoNav = 1, kNavigationDpad = 2, kNavigationTrackball = 3, kNavigationWheel = 4;

        float fontScale = 1.0f;
        int densityDpi = 160;
        int fontWeightAdjustment = 0;
        int uiMode = kUiModeTypeNormal | kUiModeNightNo;
        std::vector<std::string> locales;   // BCP 47 tags, most preferred first
        int screenWidthDp = 0, screenHeightDp = 0, smallestScreenWidthDp = 0;
        int orientation = 0;
        int screenLayout = 0;
        int touchscreen = 0, keyboard = 0, keyboardHidden = 0, hardKeyboardHidden = 0;
        int navigation = 0, navigationHidden = 0;
        int colorMode = 0;
        // The platform's Material You colours (android.R.color.system_*), in the order
        // KilnSystemColors reads them: the API 31 tonal palette, and the API 34 light then dark
        // scheme roles. Empty where the platform lacks them (or off Android).
        std::vector<std::uint32_t> systemPalette, systemRoles;

        bool IsNightModeActive() const { return (uiMode & kUiModeNightMask) == kUiModeNightYes; }
        bool IsScreenRound() const { return (screenLayout & kScreenLayoutRoundMask) == kScreenLayoutRoundYes; }
        bool IsScreenWideColorGamut() const { return (colorMode & kColorModeWideColorGamutMask) == kColorModeWideColorGamutYes; }
        bool IsScreenHdr() const { return (colorMode & kColorModeHdrMask) == kColorModeHdrYes; }
        bool operator==(const Configuration&) const = default;
    };
    inline rt::Local<Configuration> LocalConfiguration{ Configuration{} };

    // isSystemInDarkTheme: the configuration's night mode (inside a composition).
    inline bool IsSystemInDarkTheme() {
        auto* c = rt::Composition::Current();
        return c ? c->Read(LocalConfiguration).IsNightModeActive() : LocalConfiguration.Default().IsNightModeActive();
    }

    // DateFormat.is24HourFormat for the host's context; follows the setting as it changes.
    inline rt::Local<bool> LocalIs24HourFormat{ false };

    // Locale.current / LocaleList.current: the platform default locales (BCP 47), not observed,
    // like Compose's. The host sets them before content and on each change.
    namespace detail {
        inline std::mutex g_LocaleMutex;
        inline std::vector<std::string> g_Locales{ "en-US" };
    }
    inline std::vector<std::string> PlatformLocaleList() {
        std::lock_guard lock(detail::g_LocaleMutex);
        return detail::g_Locales;
    }
    inline std::string PlatformLocale() {
        std::lock_guard lock(detail::g_LocaleMutex);
        return detail::g_Locales.empty() ? std::string("en-US") : detail::g_Locales.front();
    }
    inline void SetPlatformLocaleList(std::vector<std::string> locales) {
        std::lock_guard lock(detail::g_LocaleMutex);
        detail::g_Locales = std::move(locales);
    }

    // SecureFlagPolicy (androidx.compose.ui.window): how a dialog or popup window sets
    // WindowManager.LayoutParams.FLAG_SECURE.
    enum class SecureFlagPolicy : std::uint8_t { Inherit, SecureOn, SecureOff };
    inline bool ShouldApplySecureFlag(SecureFlagPolicy policy, bool isSecureFlagSetOnParent) {
        switch (policy) {
            case SecureFlagPolicy::SecureOff: return false;
            case SecureFlagPolicy::SecureOn: return true;
            case SecureFlagPolicy::Inherit: return isSecureFlagSetOnParent;
        }
        return false;
    }

    // LocalWindow: the window the content is in (the activity's, or a dialog's). FLAG_SECURE keeps
    // it out of screenshots, recordings and non-secure displays; reading it is observable.
    class Window {
    public:
        virtual ~Window() = default;
        virtual bool IsFlagSecure() const = 0;
        virtual void SetFlagSecure(bool secure) = 0;
    };
    inline rt::Local<Window*> LocalWindow{ nullptr };

}
