#pragma once

// Platform composition locals (androidx.compose.ui.platform CompositionLocals).

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/NestedScroll.h"

#include <memory>

namespace kiln::ui {

    // LocalLayoutDirection: the host view's layout direction (RTL locales mirror layouts and
    // reverse horizontal gestures).
    inline rt::Local<LayoutDirection> LocalLayoutDirection{ LayoutDirection::Ltr };

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

    // The window's insets (WindowInsetsHolder.current: the host view's), for composables that
    // ask whether bars are visible or read insets directly.
    class WindowInsetsHolder;
    inline rt::Local<WindowInsetsHolder*> LocalWindowInsets{ nullptr };

    // WindowInfo / LocalWindowInfo: whether the host window has input focus (observable). Null
    // (no host) counts as focused.
    class WindowInfo {
    public:
        virtual ~WindowInfo() = default;
        virtual bool IsWindowFocused() const = 0;
    };
    inline rt::Local<const WindowInfo*> LocalWindowInfo{ nullptr };
    inline bool IsWindowFocused(const WindowInfo* w) { return !w || w->IsWindowFocused(); }

}
