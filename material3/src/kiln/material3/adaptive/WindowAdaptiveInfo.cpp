#include "kiln/material3/adaptive/WindowAdaptiveInfo.h"
#include "kiln/ui/Locals.h"
#include "kiln/ui/MediaQuery.h"

#include <algorithm>
#include <initializer_list>

namespace kiln::material3::adaptive {

    WindowSizeClass WindowSizeClass::ComputeFromDpSize(float widthDp, float heightDp, bool supportLargeAndXLargeWidth) {
        const auto largest = [](float size, std::initializer_list<int> breakpoints) {
            int best = 0;
            for (int b : breakpoints) if (size >= float(b)) best = std::max(best, b);
            return best;
        };
        const int width = supportLargeAndXLargeWidth
                              ? largest(widthDp, { 0, WIDTH_DP_MEDIUM_LOWER_BOUND, WIDTH_DP_EXPANDED_LOWER_BOUND, WIDTH_DP_LARGE_LOWER_BOUND,
                                                   WIDTH_DP_EXTRA_LARGE_LOWER_BOUND })
                              : largest(widthDp, { 0, WIDTH_DP_MEDIUM_LOWER_BOUND, WIDTH_DP_EXPANDED_LOWER_BOUND });
        return { width, largest(heightDp, { 0, HEIGHT_DP_MEDIUM_LOWER_BOUND, HEIGHT_DP_EXPANDED_LOWER_BOUND }) };
    }

    rt::Local<std::vector<FoldingFeature>> LocalFoldingFeatures = rt::LocalWithComputedDefault<std::vector<FoldingFeature>>([](const rt::LocalAccessorScope& s) {
        std::vector<FoldingFeature> out;
        ui::Owner* owner = s.CurrentValue(ui::LocalOwner);
        if (!owner) return out;
        // Read as state: content that reads it recomposes when the device folds.
        for (const auto& f : ui::MediaScopeOf(*owner).DisplayFolds()) {
            FoldingFeature ff;
            ff.bounds = f.bounds;
            ff.orientation = f.IsHorizontal() ? FoldingFeature::Orientation::Horizontal : FoldingFeature::Orientation::Vertical;
            ff.state = f.halfOpened ? FoldingFeature::State::HalfOpened : FoldingFeature::State::Flat;
            ff.occlusionType = f.IsOccluding() ? FoldingFeature::OcclusionType::Full : FoldingFeature::OcclusionType::None;
            ff.isSeparating = f.IsSeparating();
            out.push_back(ff);
        }
        return out;
    });

    Posture CalculatePosture(const std::vector<FoldingFeature>& foldingFeatures) {
        Posture p;
        for (const FoldingFeature& f : foldingFeatures) {
            if (f.orientation == FoldingFeature::Orientation::Horizontal && f.state == FoldingFeature::State::HalfOpened) p.isTabletop = true;
            p.hingeList.push_back({ f.bounds, f.state == FoldingFeature::State::Flat, f.orientation == FoldingFeature::Orientation::Vertical,
                                    f.isSeparating, f.occlusionType == FoldingFeature::OcclusionType::Full });
        }
        return p;
    }

    WindowAdaptiveInfo CurrentWindowAdaptiveInfo(bool supportLargeAndXLargeWidth) {
        auto& c = *rt::Composition::Current();
        const ui::WindowInfo* info = c.Read(ui::LocalWindowInfo);
        const ui::IntSize size = info ? info->ContainerSize() : ui::IntSize{};
        const float density = c.Read(ui::LocalDensity).density;
        const float widthDp = density > 0 ? float(size.width) / density : 0, heightDp = density > 0 ? float(size.height) / density : 0;
        return { WindowSizeClass::ComputeFromDpSize(widthDp, heightDp, supportLargeAndXLargeWidth), CalculatePosture(c.Read(LocalFoldingFeatures)) };
    }

}
