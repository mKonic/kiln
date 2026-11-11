#pragma once

// WindowInsets for layout (foundation-layout WindowInsets.kt, WindowInsets.android.kt,
// WindowInsetsPadding.kt, WindowInsetsSize.kt): insets as values that combine (union, add,
// exclude, only some sides), padding modifiers that pad by what ancestors have not consumed yet,
// consumption without padding, size modifiers and visibility queries.

#include "kiln/foundation/Layouts.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Insets.h"

#include <functional>
#include <memory>

namespace kiln::foundation {

    // WindowInsetsSides: Start / End follow the layout direction; Left / Right do not.
    struct WindowInsetsSides {
        std::uint8_t value = 0;
        static constexpr std::uint8_t kAllowLeftInLtr = 1 << 3, kAllowRightInLtr = 1 << 2, kAllowLeftInRtl = 1 << 1, kAllowRightInRtl = 1 << 0;
        static constexpr WindowInsetsSides Start() { return { kAllowLeftInLtr | kAllowRightInRtl }; }
        static constexpr WindowInsetsSides End() { return { kAllowRightInLtr | kAllowLeftInRtl }; }
        static constexpr WindowInsetsSides Top() { return { 1 << 4 }; }
        static constexpr WindowInsetsSides Bottom() { return { 1 << 5 }; }
        static constexpr WindowInsetsSides Left() { return { kAllowLeftInLtr | kAllowLeftInRtl }; }
        static constexpr WindowInsetsSides Right() { return { kAllowRightInLtr | kAllowRightInRtl }; }
        static constexpr WindowInsetsSides Horizontal() { return { std::uint8_t(Left().value | Right().value) }; }
        static constexpr WindowInsetsSides Vertical() { return { std::uint8_t(Top().value | Bottom().value) }; }
        constexpr WindowInsetsSides operator+(WindowInsetsSides o) const { return { std::uint8_t(value | o.value) }; }
        constexpr bool HasAny(WindowInsetsSides o) const { return (value & o.value) != 0; }
        bool operator==(const WindowInsetsSides&) const = default;
    };

    // Where insets are read: the window's holder (null: window insets read as zero).
    struct InsetsContext {
        const ui::WindowInsetsHolder* holder = nullptr;
        Density density;
        LayoutDirection direction = LayoutDirection::Ltr;
    };

    class WindowInsets {
    public:
        WindowInsets();   // no insets
        int Left(const InsetsContext& c) const;
        int Top(const InsetsContext& c) const;
        int Right(const InsetsContext& c) const;
        int Bottom(const InsetsContext& c) const;

        // union: the larger value per side; add: the sum; exclude: the difference (not below 0);
        // only: zero outside the sides.
        WindowInsets Union(const WindowInsets& o) const;
        WindowInsets Add(const WindowInsets& o) const;
        WindowInsets Exclude(const WindowInsets& o) const;
        WindowInsets Only(WindowInsetsSides sides) const;
        // asPaddingValues: the insets now, as absolute padding.
        PaddingValues AsPaddingValues(const InsetsContext& c) const;
        bool operator==(const WindowInsets& o) const;

        static WindowInsets Fixed(int left, int top, int right, int bottom);
        static WindowInsets Fixed(Dp left, Dp top, Dp right, Dp bottom);
        static WindowInsets FromPadding(PaddingValues p);   // PaddingValues.asInsets
        // Whatever `state` holds when read (material3's MutableWindowInsets): layouts that read
        // it follow its changes.
        static WindowInsets FromState(std::shared_ptr<const rt::State<WindowInsets>> state);
        // A WindowInsets implementation: `side` answers each side (0 left, 1 top, 2 right,
        // 3 bottom) when read; state it reads is observed by the layouts that read it. Equal to
        // another with the same non-null `identity` (its equals), else only to its copies.
        using SideFunction = std::function<int(const InsetsContext& c, int side)>;
        static WindowInsets FromFunction(SideFunction side, const void* identity = nullptr);

        // The window's insets by type.
        static WindowInsets CaptionBar();
        static WindowInsets DisplayCutout();
        static WindowInsets Ime();
        static WindowInsets MandatorySystemGestures();
        static WindowInsets NavigationBars();
        static WindowInsets StatusBars();
        static WindowInsets SystemGestures();
        static WindowInsets TappableElement();
        static WindowInsets Waterfall();
        static WindowInsets SystemBars();      // status + navigation + caption bars
        static WindowInsets SafeDrawing();     // system bars + display cutout + IME
        static WindowInsets SafeGestures();    // tappable element + mandatory + system gestures + waterfall
        static WindowInsets SafeContent();     // safe drawing + safe gestures
        // What the bars would take while hidden (getInsetsIgnoringVisibility).
        static WindowInsets CaptionBarIgnoringVisibility();
        static WindowInsets NavigationBarsIgnoringVisibility();
        static WindowInsets StatusBarsIgnoringVisibility();
        static WindowInsets SystemBarsIgnoringVisibility();
        static WindowInsets TappableElementIgnoringVisibility();
        // Where a running IME animation starts and ends (both the current IME insets otherwise).
        static WindowInsets ImeAnimationSource();
        static WindowInsets ImeAnimationTarget();

        struct Node;

    private:
        explicit WindowInsets(std::shared_ptr<const Node> n) : m_Node(std::move(n)) {}
        std::shared_ptr<const Node> m_Node;
    };

    // Visibility (WindowInsets.android.kt), read from LocalWindowInsets: false without a window.
    bool IsCaptionBarVisible();
    bool IsImeVisible();
    bool AreStatusBarsVisible();
    bool AreNavigationBarsVisible();
    bool AreSystemBarsVisible();
    bool IsTappableElementVisible();

    // windowInsetsPadding: pads by the insets the ancestors have not consumed, and consumes them.
    Modifier WindowInsetsPadding(Modifier m, WindowInsets insets);
    // consumeWindowInsets: descendants see these insets as consumed (no padding here).
    Modifier ConsumeWindowInsets(Modifier m, WindowInsets insets);
    Modifier ConsumeWindowInsets(Modifier m, PaddingValues padding);
    // onConsumedWindowInsetsChanged: what the ancestors have consumed.
    Modifier OnConsumedWindowInsetsChanged(Modifier m, std::function<void(const WindowInsets&)> block);

    inline Modifier SafeDrawingPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::SafeDrawing()); }
    inline Modifier SafeGesturesPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::SafeGestures()); }
    inline Modifier SafeContentPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::SafeContent()); }
    inline Modifier SystemBarsPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::SystemBars()); }
    inline Modifier DisplayCutoutPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::DisplayCutout()); }
    inline Modifier StatusBarsPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::StatusBars()); }
    inline Modifier ImePadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::Ime()); }
    inline Modifier NavigationBarsPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::NavigationBars()); }
    inline Modifier CaptionBarPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::CaptionBar()); }
    inline Modifier WaterfallPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::Waterfall()); }
    inline Modifier SystemGesturesPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::SystemGestures()); }
    inline Modifier MandatorySystemGesturesPadding(Modifier m) { return WindowInsetsPadding(std::move(m), WindowInsets::MandatorySystemGestures()); }

    // imeNestedScroll (API 30+): scrolling content at the bottom pulls the keyboard open or pushes
    // it closed, and a fling finishes the motion (WindowInsetsConnection.android.kt).
    Modifier ImeNestedScroll(Modifier m);

    // windowInsetsTopHeight / BottomHeight / StartWidth / EndWidth: a size of that side of the
    // insets (minus what ancestors consumed).
    Modifier WindowInsetsTopHeight(Modifier m, WindowInsets insets);
    Modifier WindowInsetsBottomHeight(Modifier m, WindowInsets insets);
    Modifier WindowInsetsStartWidth(Modifier m, WindowInsets insets);
    Modifier WindowInsetsEndWidth(Modifier m, WindowInsets insets);

}
