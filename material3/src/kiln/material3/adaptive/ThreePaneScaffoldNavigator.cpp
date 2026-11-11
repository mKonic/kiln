#include "kiln/material3/adaptive/ThreePaneScaffoldNavigator.h"

#include "kiln/animation/Easing.h"

namespace kiln::material3::adaptive::detail {

    ThreePaneScaffoldValue CalculateScaffoldValue(const PaneScaffoldDirective& directive, const ThreePaneScaffoldAdaptStrategies& strategies,
                                                  bool historyAware, const std::vector<ThreePaneScaffoldRole>& panes, int index) {
        if (index < 0)
            return CalculateThreePaneScaffoldValue(directive.maxHorizontalPartitions, strategies, std::optional<ThreePaneScaffoldRole>{},
                                                   directive.maxVerticalPartitions);
        if (historyAware) {
            const std::vector<ThreePaneScaffoldRole> history(panes.begin(), panes.begin() + index + 1);
            return CalculateThreePaneScaffoldValue(directive.maxHorizontalPartitions, strategies, history, directive.maxVerticalPartitions);
        }
        return CalculateThreePaneScaffoldValue(directive.maxHorizontalPartitions, strategies, std::optional(panes[std::size_t(index)]),
                                               directive.maxVerticalPartitions);
    }

    std::size_t SettingsHash(const PaneScaffoldDirective& d, const ThreePaneScaffoldAdaptStrategies& s, bool historyAware) {
        std::size_t h = std::size_t(d.maxHorizontalPartitions) * 31 + std::size_t(d.maxVerticalPartitions);
        h = h * 31 + std::hash<float>{}(d.horizontalPartitionSpacerSize.value) + std::hash<float>{}(d.verticalPartitionSpacerSize.value) * 7;
        h = h * 31 + std::hash<float>{}(d.defaultPanePreferredWidth.value) + std::hash<float>{}(d.defaultPanePreferredHeight.value) * 7;
        for (const ui::Rect& r : d.excludedBounds) h = h * 31 + std::hash<float>{}(r.left) + std::hash<float>{}(r.right) * 3;
        for (const AdaptStrategy* a : { &s.primary, &s.secondary, &s.tertiary }) {
            h = h * 31 + std::size_t(a->kind) * 5 + std::size_t(a->reflowUnder);
            h = h * 31 + std::hash<const void*>{}(a->scrim.get()) + std::hash<const void*>{}(a->dragToResizeState.get());
        }
        return h * 2 + (historyAware ? 1 : 0);
    }

    float BackProgressToStateProgress(float progress, const ThreePaneScaffoldValue& value) {
        static const anim::Easing easing = anim::Easing::CubicBezier(0.1f, 0.1f, 0.0f, 1.0f);
        int expanded = 0;
        value.ForEach([&](ThreePaneScaffoldRole, const PaneAdaptedValue& v) { if (v.kind == PaneAdaptedValue::Kind::Expanded) ++expanded; });
        const float ratio = expanded == 1 ? 0.1f : expanded == 2 ? 0.15f : 0.2f;
        return easing.Transform(progress) * ratio;
    }

}
