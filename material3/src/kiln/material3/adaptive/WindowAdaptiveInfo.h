#pragma once

// material3-adaptive's WindowAdaptiveInfo.kt / Posture.kt / WindowSizeClassHelper.kt, with
// androidx.window's WindowSizeClass: the window's size class and posture.

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Geometry.h"

#include <cstdint>
#include <vector>

namespace kiln::material3::adaptive {

    // androidx.window.core.layout.WindowSizeClass: the breakpoints a window's size reaches.
    struct WindowSizeClass {
        static constexpr int WIDTH_DP_MEDIUM_LOWER_BOUND = 600, WIDTH_DP_EXPANDED_LOWER_BOUND = 840, WIDTH_DP_LARGE_LOWER_BOUND = 1200,
                             WIDTH_DP_EXTRA_LARGE_LOWER_BOUND = 1600;
        static constexpr int HEIGHT_DP_MEDIUM_LOWER_BOUND = 480, HEIGHT_DP_EXPANDED_LOWER_BOUND = 900;

        int minWidthDp = 0, minHeightDp = 0;

        bool IsWidthAtLeastBreakpoint(int widthDpBreakpoint) const { return minWidthDp >= widthDpBreakpoint; }
        bool IsHeightAtLeastBreakpoint(int heightDpBreakpoint) const { return minHeightDp >= heightDpBreakpoint; }
        bool IsAtLeastBreakpoint(int widthDpBreakpoint, int heightDpBreakpoint) const {
            return IsWidthAtLeastBreakpoint(widthDpBreakpoint) && IsHeightAtLeastBreakpoint(heightDpBreakpoint);
        }
        bool operator==(const WindowSizeClass&) const = default;

        // The deprecated windowWidthSizeClass / windowHeightSizeClass: Compact, Medium or
        // Expanded by the lower bound this class reaches.
        enum class WidthClass : std::uint8_t { Compact, Medium, Expanded };
        enum class HeightClass : std::uint8_t { Compact, Medium, Expanded };
        WidthClass WindowWidthSizeClass() const {
            return minWidthDp >= WIDTH_DP_EXPANDED_LOWER_BOUND ? WidthClass::Expanded
                   : minWidthDp >= WIDTH_DP_MEDIUM_LOWER_BOUND ? WidthClass::Medium
                                                               : WidthClass::Compact;
        }
        HeightClass WindowHeightSizeClass() const {
            return minHeightDp >= HEIGHT_DP_EXPANDED_LOWER_BOUND ? HeightClass::Expanded
                   : minHeightDp >= HEIGHT_DP_MEDIUM_LOWER_BOUND ? HeightClass::Medium
                                                                 : HeightClass::Compact;
        }

        // computeFromDpSize: the largest supported breakpoint the size reaches on each axis
        // (Compact / Medium / Expanded wide; V2 adds Large and ExtraLarge).
        static WindowSizeClass ComputeFromDpSize(float widthDp, float heightDp, bool supportLargeAndXLargeWidth = false);
    };

    // A fold or hinge, in window pixels.
    struct HingeInfo {
        ui::Rect bounds;
        bool isFlat = false, isVertical = false, isSeparating = false, isOccluding = false;
        bool operator==(const HingeInfo&) const = default;
    };

    // androidx.window's FoldingFeature, as the host reports it.
    struct FoldingFeature {
        enum class Orientation : std::uint8_t { Vertical, Horizontal };
        enum class State : std::uint8_t { Flat, HalfOpened };
        enum class OcclusionType : std::uint8_t { None, Full };
        ui::Rect bounds;
        Orientation orientation = Orientation::Vertical;
        State state = State::Flat;
        OcclusionType occlusionType = OcclusionType::None;
        bool isSeparating = false;
    };
    // The window's folding features (collectFoldingFeaturesAsState); empty where the host has none.
    inline rt::Local<std::vector<FoldingFeature>> LocalFoldingFeatures{ {} };

    struct Posture {
        bool isTabletop = false;
        std::vector<HingeInfo> hingeList;
        bool operator==(const Posture&) const = default;

        // The bounds of the hinges of each kind, in order.
        std::vector<ui::Rect> SeparatingVerticalHingeBounds() const { return Bounds([](const HingeInfo& h) { return h.isVertical && h.isSeparating; }); }
        std::vector<ui::Rect> OccludingVerticalHingeBounds() const { return Bounds([](const HingeInfo& h) { return h.isVertical && h.isOccluding; }); }
        std::vector<ui::Rect> AllVerticalHingeBounds() const { return Bounds([](const HingeInfo& h) { return h.isVertical; }); }
        std::vector<ui::Rect> SeparatingHorizontalHingeBounds() const { return Bounds([](const HingeInfo& h) { return !h.isVertical && h.isSeparating; }); }
        std::vector<ui::Rect> OccludingHorizontalHingeBounds() const { return Bounds([](const HingeInfo& h) { return !h.isVertical && h.isOccluding; }); }
        std::vector<ui::Rect> AllHorizontalHingeBounds() const { return Bounds([](const HingeInfo& h) { return !h.isVertical; }); }

    private:
        template<typename P> std::vector<ui::Rect> Bounds(P predicate) const {
            std::vector<ui::Rect> out;
            for (const HingeInfo& h : hingeList) if (predicate(h)) out.push_back(h.bounds);
            return out;
        }
    };
    // Tabletop when a horizontal fold is half open; every fold becomes a hinge.
    Posture CalculatePosture(const std::vector<FoldingFeature>& foldingFeatures);

    struct WindowAdaptiveInfo {
        WindowSizeClass windowSizeClass;
        Posture windowPosture;
        bool operator==(const WindowAdaptiveInfo&) const = default;
    };
    // currentWindowAdaptiveInfo / currentWindowAdaptiveInfoV2 (V2: supportLargeAndXLargeWidth):
    // from LocalWindowInfo's containerSize and LocalDensity; recomposes on resize.
    WindowAdaptiveInfo CurrentWindowAdaptiveInfo(bool supportLargeAndXLargeWidth = false);
    inline WindowAdaptiveInfo CurrentWindowAdaptiveInfoV2() { return CurrentWindowAdaptiveInfo(true); }

}
