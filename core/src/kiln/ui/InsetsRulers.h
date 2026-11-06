#pragma once

// WindowInsetsRulers (ui layout/WindowInsetsRulers.kt, WindowInsetsRulersProvider.android.kt):
// the window insets as rulers the root provides to every layout, read during placement without
// recomposition. Each type has its current and maximum (ignoring visibility) bounds, and while
// it animates, the animation's source and target bounds.

#include "kiln/ui/Insets.h"
#include "kiln/ui/Layout.h"

#include <vector>

namespace kiln::ui {

    // WindowInsetsAnimation for a type: what getAnimation returns.
    struct WindowInsetsAnimationInfo {
        RectRulers source, target;   // never provided for combined rulers and the waterfall
        bool isVisible = true;
        bool isAnimating = false;
        float fraction = 0;
        std::int64_t durationMillis = 0;
        float alpha = 1;
    };

    class WindowInsetsRulers {
    public:
        const RectRulers& Current() const { return m_Current; }
        const RectRulers& Maximum() const { return m_Maximum; }
        WindowInsetsAnimationInfo GetAnimation(PlacementScope& scope) const;

        static const WindowInsetsRulers& CaptionBar();
        static const WindowInsetsRulers& DisplayCutout();
        static const WindowInsetsRulers& Ime();
        static const WindowInsetsRulers& MandatorySystemGestures();
        static const WindowInsetsRulers& NavigationBars();
        static const WindowInsetsRulers& StatusBars();
        static const WindowInsetsRulers& SystemGestures();
        static const WindowInsetsRulers& TappableElement();
        static const WindowInsetsRulers& Waterfall();
        // Innermost of StatusBars, NavigationBars, CaptionBar.
        static const WindowInsetsRulers& SystemBars();
        // Innermost of the bars, DisplayCutout, Ime, TappableElement.
        static const WindowInsetsRulers& SafeDrawing();
        // Innermost of MandatorySystemGestures, SystemGestures, TappableElement, Waterfall.
        static const WindowInsetsRulers& SafeGestures();
        // Innermost of all of the above.
        static const WindowInsetsRulers& SafeContent();
        static WindowInsetsRulers InnermostOf(std::vector<const WindowInsetsRulers*> rulers);

        // The single type behind these rulers (Count for combined ones).
        InsetsType Type() const { return m_Type; }

    private:
        WindowInsetsRulers(InsetsType type);
        explicit WindowInsetsRulers(std::vector<const WindowInsetsRulers*> parts);
        InsetsType m_Type = InsetsType::Count;
        RectRulers m_Current, m_Maximum;
        std::vector<const WindowInsetsRulers*> m_Parts;
    };

    // getDisplayCutoutBounds: rulers around each cutout (up to four).
    std::vector<RectRulers> GetDisplayCutoutBounds(PlacementScope& scope);

    // The root's ruler values for the holder (window pixels, the root `width` x `height`).
    void ProvideWindowInsetsRulers(RulerScope& scope, const WindowInsetsHolder& insets, int width, int height);

}
