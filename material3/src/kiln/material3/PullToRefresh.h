#pragma once

// pulltorefresh/PullToRefresh.kt: pulling scrolled-to-the-top content down past a threshold
// asks for a refresh; an indicator follows the pull (an arrow that closes into a circle, or the
// Expressive loading indicator) and spins while refreshing.

#include "kiln/foundation/Shapes.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/Layout.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>

namespace kiln::material3 {

    // How far the indicator is pulled: 0 hidden, 1 at the threshold, more when pulled past it.
    class PullToRefreshState {
    public:
        virtual ~PullToRefreshState() = default;
        virtual float DistanceFraction() const = 0;   // reading it subscribes
        virtual bool IsAnimating() const = 0;
        virtual rt::Task<> AnimateToThreshold() = 0;
        virtual rt::Task<> AnimateToHidden() = 0;
        virtual rt::Task<> SnapTo(float targetValue) = 0;
    };

    // PullToRefreshState(): the default, animated with a spring.
    std::shared_ptr<PullToRefreshState> MakePullToRefreshState();
    // rememberPullToRefreshState: the fraction survives re-creation.
    std::shared_ptr<PullToRefreshState> RememberPullToRefreshState();

    namespace PullToRefreshDefaults {
        extern const ui::Dp PositionalThreshold, IndicatorMaxDistance, Elevation, LoadingIndicatorElevation;
        foundation::ShapePtr indicatorShape();
        ui::Color indicatorContainerColor();
        ui::Color loadingIndicatorContainerColor();
        ui::Color indicatorColor();
        ui::Color loadingIndicatorColor();

        struct IndicatorBoxOptions {
            ui::Dp maxDistance = IndicatorMaxDistance;
            foundation::ShapePtr shape;                     // null: indicatorShape
            ui::Color containerColor = ui::Color::Unspecified();
            ui::Dp elevation = Elevation;
        };
        // A 40 dp container that slides down with the pull (maxDistance at the threshold),
        // clipped above its resting place, with a shadow once it shows.
        void IndicatorBox(std::shared_ptr<PullToRefreshState> state, bool isRefreshing, const ui::Modifier& modifier, const IndicatorBoxOptions& options,
                          std::function<void()> content);

        struct IndicatorOptions {
            std::optional<ui::Color> containerColor, color;   // unset: the defaults (per indicator)
            ui::Dp maxDistance = IndicatorMaxDistance;
            std::optional<ui::Dp> elevation;                  // LoadingIndicator only; unset: LoadingIndicatorElevation
        };
        // The arrow that grows into a circle as it is pulled, then a spinner while refreshing.
        void Indicator(std::shared_ptr<PullToRefreshState> state, bool isRefreshing, const ui::Modifier& modifier = {}, const IndicatorOptions& options = {});
        // The Expressive form: the contained loading indicator morphing with the pull (turning
        // past the threshold), then animating while refreshing.
        void LoadingIndicator(std::shared_ptr<PullToRefreshState> state, bool isRefreshing, const ui::Modifier& modifier = {},
                              const IndicatorOptions& options = {});
    }

    // Modifier.pullToRefresh: content inside that is scrolled to its top, pulled down past
    // `threshold`, calls onRefresh on release. Call it during composition.
    ui::Modifier PullToRefresh(ui::Modifier m, bool isRefreshing, std::shared_ptr<PullToRefreshState> state, std::function<void()> onRefresh,
                               bool enabled = true, ui::Dp threshold = PullToRefreshDefaults::PositionalThreshold);

    struct PullToRefreshBoxOptions {
        std::shared_ptr<PullToRefreshState> state;   // null: remembered
        ui::Alignment contentAlignment = ui::Alignment::TopStart();
        std::function<void()> indicator;             // null: Indicator at the top centre
        bool enabled = true;
        ui::Dp threshold = PullToRefreshDefaults::PositionalThreshold;
    };
    void PullToRefreshBox(bool isRefreshing, std::function<void()> onRefresh, const ui::Modifier& modifier, const PullToRefreshBoxOptions& options,
                          std::function<void()> content);

    namespace internal {
        inline constexpr ui::Dp SpinnerSize{ 16 }, SpinnerContainerSize{ 40 };
    }

}
