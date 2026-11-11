#pragma once

// Platform composition locals (androidx.compose.ui.platform CompositionLocals).

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/NestedScroll.h"

#include <cstdint>
#include <memory>

namespace kiln::ui {

    // LocalLayoutDirection: the host view's layout direction (RTL locales mirror layouts and
    // reverse horizontal gestures).
    inline rt::Local<LayoutDirection> LocalLayoutDirection{ LayoutDirection::Ltr };
    // LocalDensity: the window's density (px per dp) and font scale.
    inline rt::Local<Density> LocalDensity{ Density{} };

    // The host view's nested-scrolling parent (LocalView for rememberNestedScrollInteropConnection);
    // null when the host has none.
    inline rt::Local<HostNestedScrollParent*> LocalHostNestedScrollParent{ nullptr };

    // PinnableContainer: content that matters right now (a focused field) pins its container, and
    // a lazy item stays composed and laid out while pinned even when scrolled away. Pins count.
    class PinnableContainer {
    public:
        virtual ~PinnableContainer() = default;
        virtual void Pin() = 0;
        virtual void Release() = 0;
    };
    // The pinnable container around this content (a lazy item), or null.
    inline rt::Local<std::shared_ptr<PinnableContainer>> LocalPinnableContainer{ nullptr };

    // The device is a TV (FEATURE_LEANBACK): focus scrolling pivots (LocalBringIntoViewSpec).
    inline rt::Local<bool> LocalIsTelevision{ false };

    // SoundEffect / LocalSoundEffect: the click sound clickables play (View.playSoundEffect).
    class SoundEffect {
    public:
        virtual ~SoundEffect() = default;
        virtual void PlayClickSound() = 0;
    };
    inline rt::Local<std::shared_ptr<SoundEffect>> LocalSoundEffect{ nullptr };

    // The accessibility services that are on (AccessibilityManager and its listeners).
    struct AccessibilityServiceState {
        bool enabled = false, touchExploration = false, switchAccess = false, voiceAccess = false;
        bool operator==(const AccessibilityServiceState&) const = default;
    };
    // AccessibilityManager / LocalAccessibilityManager.
    class AccessibilityManager {
    public:
        virtual ~AccessibilityManager() = default;
        // calculateRecommendedTimeoutMillis: the user's preferred time for content to stay up
        // (INT64_MAX: until dismissed).
        virtual std::int64_t CalculateRecommendedTimeoutMillis(std::int64_t originalTimeoutMillis, bool containsIcons = false,
                                                               bool containsText = false, bool containsControls = false) const = 0;
        // The services now (observable).
        virtual AccessibilityServiceState ServiceState() const = 0;
    };
    inline rt::Local<std::shared_ptr<const AccessibilityManager>> LocalAccessibilityManager{ nullptr };
    // rememberAccessibilityServiceState: an accessibility service the caller cares about is on.
    inline bool AccessibilityServiceOn(const AccessibilityServiceState& s, bool touchExploration = true, bool switchAccess = true,
                                       bool voiceAccess = true) {
        return s.enabled && ((touchExploration && s.touchExploration) || (switchAccess && s.switchAccess) || (voiceAccess && s.voiceAccess));
    }

    // LocalActivityResultRegistryOwner's registry (the host's), for activity result launchers.
    class ActivityResultRegistry;
    inline rt::Local<ActivityResultRegistry*> LocalActivityResultRegistry{ nullptr };

    // LocalScrollCaptureInProgress: a long screenshot is being taken.
    inline rt::Local<bool> LocalScrollCaptureInProgress{ false };

    // HapticFeedbackType: the platform's HapticFeedbackConstants (HapticFeedbackConstantsCompat).
    enum class HapticFeedbackType : int {
        Confirm = 16, ContextClick = 6, GestureEnd = 13, GestureThresholdActivate = 23, KeyboardTap = 3, LongPress = 0,
        Reject = 17, SegmentFrequentTick = 27, SegmentTick = 26, TextHandleMove = 9, ToggleOff = 22, ToggleOn = 21, VirtualKey = 1,
    };
    // HapticFeedback / LocalHapticFeedback (PlatformHapticFeedback on the host view).
    class HapticFeedback {
    public:
        virtual ~HapticFeedback() = default;
        virtual void PerformHapticFeedback(HapticFeedbackType type) = 0;
    };
    inline rt::Local<std::shared_ptr<HapticFeedback>> LocalHapticFeedback{ nullptr };

    // The window's insets (WindowInsetsHolder.current: the host view's), for composables that
    // ask whether bars are visible or read insets directly.
    // LocalOwner: the owner of the window the content is in (what LocalView / LocalContext give
    // Compose content: the platform services behind it).
    class Owner;
    inline rt::Local<Owner*> LocalOwner{ nullptr };

    // SoftwareKeyboardController: shows or hides the keyboard for the focused text field.
    class SoftwareKeyboardController {
    public:
        virtual ~SoftwareKeyboardController() = default;
        virtual void Show() = 0;
        virtual void Hide() = 0;
    };
    // LocalSoftwareKeyboardController. Unprovided it is the owner's, as Compose computes its
    // default from LocalOwner: read it with CurrentSoftwareKeyboardController().
    inline rt::Local<std::shared_ptr<SoftwareKeyboardController>> LocalSoftwareKeyboardController{ nullptr };
    std::shared_ptr<SoftwareKeyboardController> CurrentSoftwareKeyboardController();

    class WindowInsetsHolder;
    inline rt::Local<WindowInsetsHolder*> LocalWindowInsets{ nullptr };

    // WindowInfo / LocalWindowInfo: whether the host window has input focus, and its size
    // (observable). Null (no host) counts as focused.
    class WindowInfo {
    public:
        virtual ~WindowInfo() = default;
        virtual bool IsWindowFocused() const = 0;
        // containerSize: the window's size in pixels; reading it recomposes on resize.
        virtual IntSize ContainerSize() const = 0;
    };
    inline rt::Local<const WindowInfo*> LocalWindowInfo{ nullptr };
    inline bool IsWindowFocused(const WindowInfo* w) { return !w || w->IsWindowFocused(); }

}
