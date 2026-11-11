#include "kiln/material3/adaptive/PaneScaffoldValue.h"

namespace kiln::material3::adaptive {

    using ui::Dp;

    namespace {
        std::vector<ui::Rect> ExcludedVerticalBounds(const Posture& posture, HingePolicy policy) {
            switch (policy) {
                case HingePolicy::AvoidSeparating: return posture.SeparatingVerticalHingeBounds();
                case HingePolicy::AvoidOccluding: return posture.OccludingVerticalHingeBounds();
                case HingePolicy::AlwaysAvoid: return posture.AllVerticalHingeBounds();
                case HingePolicy::NeverAvoid: break;
            }
            return {};
        }
    }

    PaneScaffoldDirective CalculatePaneScaffoldDirective(const WindowAdaptiveInfo& info, HingePolicy verticalHingePolicy) {
        PaneScaffoldDirective d;
        switch (info.windowSizeClass.minWidthDp) {
            case 0:   // WidthSizeClasses.Compact
            case WindowSizeClass::WIDTH_DP_MEDIUM_LOWER_BOUND:
                d.maxHorizontalPartitions = 1;
                d.horizontalPartitionSpacerSize = Dp(0);
                d.defaultPanePreferredWidth = PaneScaffoldDirective::DefaultPreferredWidth;
                break;
            case WindowSizeClass::WIDTH_DP_EXPANDED_LOWER_BOUND:
                d.maxHorizontalPartitions = 2;
                d.horizontalPartitionSpacerSize = Dp(24);
                d.defaultPanePreferredWidth = PaneScaffoldDirective::DefaultPreferredWidth;
                break;
            default:
                d.maxHorizontalPartitions = 3;
                d.horizontalPartitionSpacerSize = Dp(24);
                d.defaultPanePreferredWidth = PaneScaffoldDirective::DefaultPreferredWidthXL;
                break;
        }
        if (info.windowPosture.isTabletop ||
            (d.maxHorizontalPartitions == 1 && info.windowSizeClass.minHeightDp == WindowSizeClass::HEIGHT_DP_EXPANDED_LOWER_BOUND)) {
            d.maxVerticalPartitions = 2;
            d.verticalPartitionSpacerSize = Dp(24);
        } else {
            d.maxVerticalPartitions = 1;
            d.verticalPartitionSpacerSize = Dp(0);
        }
        d.defaultPanePreferredHeight = PaneScaffoldDirective::DefaultPreferredHeight;
        d.excludedBounds = ExcludedVerticalBounds(info.windowPosture, verticalHingePolicy);
        return d;
    }

    PaneScaffoldDirective CalculatePaneScaffoldDirectiveWithTwoPanesOnMediumWidth(const WindowAdaptiveInfo& info, HingePolicy verticalHingePolicy) {
        const bool isMediumWidth = info.windowSizeClass.minWidthDp == WindowSizeClass::WIDTH_DP_MEDIUM_LOWER_BOUND;
        const bool isTableTop = info.windowPosture.isTabletop;
        PaneScaffoldDirective d = CalculatePaneScaffoldDirective(info, verticalHingePolicy);
        // copy() keeps the defaults for shouldAutoFocusCurrentDestination.
        d.shouldAutoFocusCurrentDestination = true;
        if (isMediumWidth) {
            d.maxHorizontalPartitions = 2;
            d.horizontalPartitionSpacerSize = Dp(24);
            if (!isTableTop) {
                d.maxVerticalPartitions = 1;
                d.verticalPartitionSpacerSize = Dp(0);
            }
        }
        return d;
    }

    int ThreePaneScaffoldValue::ExpandedCount() const {
        int count = 0;
        ForEach([&](ThreePaneScaffoldRole, const PaneAdaptedValue& v) { if (v.kind == PaneAdaptedValue::Kind::Expanded) ++count; });
        return count;
    }

    PaneExpansionStateKey ThreePaneScaffoldValue::ExpansionStateKey() const {
        if (ExpandedCount() != 2) return {};
        std::vector<ThreePaneScaffoldRole> expanded;
        ForEach([&](ThreePaneScaffoldRole r, const PaneAdaptedValue& v) { if (v.kind == PaneAdaptedValue::Kind::Expanded) expanded.push_back(r); });
        return { std::pair{ expanded[0], expanded[1] } };
    }

    bool ThreePaneScaffoldValue::IsInteractable(ThreePaneScaffoldRole role) const {
        const PaneAdaptedValue& v = (*this)[role];
        if (v.kind == PaneAdaptedValue::Kind::Hidden) return false;
        if (v.kind == PaneAdaptedValue::Kind::Levitated) return true;
        bool levitatedWithScrim = false;
        ForEach([&](ThreePaneScaffoldRole, const PaneAdaptedValue& p) {
            if (p.kind == PaneAdaptedValue::Kind::Levitated && p.scrim) levitatedWithScrim = true;
        });
        return !levitatedWithScrim;
    }

    ThreePaneScaffoldValue CalculateThreePaneScaffoldValue(int maxHorizontalPartitions, const ThreePaneScaffoldAdaptStrategies& strategies,
                                                           const std::vector<ThreePaneScaffoldRole>& history, int maxVerticalPartitions) {
        int expandedCount = 0;
        std::optional<PaneAdaptedValue> values[3];
        auto at = [&](ThreePaneScaffoldRole r) -> std::optional<PaneAdaptedValue>& { return values[int(r)]; };
        const auto isReflow = [&](ThreePaneScaffoldRole r) { return strategies[r].kind == AdaptStrategy::Kind::Reflow; };
        bool checkReflowedPane = maxHorizontalPartitions == 1 && maxVerticalPartitions > 1 &&
                                 (isReflow(ThreePaneScaffoldRole::Primary) || isReflow(ThreePaneScaffoldRole::Secondary) ||
                                  isReflow(ThreePaneScaffoldRole::Tertiary));
        // Only the current destination may levitate.
        if (!history.empty()) {
            const ThreePaneScaffoldRole pane = history.back();
            const AdaptStrategy& s = strategies[pane];
            if (s.kind == AdaptStrategy::Kind::Levitate) at(pane) = PaneAdaptedValue::Levitated(s.alignment, s.scrim, s.dragToResizeState);
        }
        // forEachPaneByPriority: the history, latest first, then the panes in order.
        std::vector<ThreePaneScaffoldRole> byPriority(history.rbegin(), history.rend());
        byPriority.insert(byPriority.end(), { ThreePaneScaffoldRole::Primary, ThreePaneScaffoldRole::Secondary, ThreePaneScaffoldRole::Tertiary });
        for (const ThreePaneScaffoldRole pane : byPriority) {
            const bool hasAvailablePartition = expandedCount < maxHorizontalPartitions;
            if (!hasAvailablePartition && !checkReflowedPane) break;
            if (at(pane)) continue;   // already adapted
            std::optional<ThreePaneScaffoldRole> reflowedPane;
            ThreePaneScaffoldRole anchorPane = pane;
            std::optional<PaneAdaptedValue> anchorValue;
            if (checkReflowedPane && strategies[pane].kind == AdaptStrategy::Kind::Reflow) {
                reflowedPane = pane;
                anchorPane = strategies[pane].reflowUnder;
                anchorValue = at(anchorPane);
            }
            if (!anchorValue) {
                if (strategies[anchorPane].kind == AdaptStrategy::Kind::Levitate) continue;   // the anchor can only levitate
                if (!hasAvailablePartition) continue;
                at(anchorPane) = PaneAdaptedValue::Expanded();   // expanded to take the reflowed pane
                ++expandedCount;
            } else if (anchorValue->kind != PaneAdaptedValue::Kind::Expanded) {
                continue;
            }
            if (reflowedPane) {
                at(*reflowedPane) = PaneAdaptedValue::Reflowed(anchorPane);
                checkReflowedPane = false;
            }
        }
        ThreePaneScaffoldValue v;
        v.primary = values[0].value_or(PaneAdaptedValue::Hidden());
        v.secondary = values[1].value_or(PaneAdaptedValue::Hidden());
        v.tertiary = values[2].value_or(PaneAdaptedValue::Hidden());
        if (!history.empty()) v.currentDestination = history.back();
        return v;
    }

}
