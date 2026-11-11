#pragma once

// adaptive-layout's pane scaffold model: PaneScaffoldDirective.kt (how many panes the window
// fits, and their spacing), AdaptStrategy.kt / PaneAdaptedValue.kt (what a pane that does not
// fit does: hide, reflow under another pane, or levitate over the others), and
// ThreePaneScaffoldValue.kt (which of the three panes show, by the destination history).

#include "kiln/material3/adaptive/WindowAdaptiveInfo.h"
#include "kiln/ui/Geometry.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::material3::adaptive {

    enum class ThreePaneScaffoldRole : std::uint8_t { Primary, Secondary, Tertiary };

    // HingePolicy: which vertical hinges panes keep clear of.
    enum class HingePolicy : std::uint8_t { AlwaysAvoid, AvoidSeparating, AvoidOccluding, NeverAvoid };

    struct PaneScaffoldDirective {
        int maxHorizontalPartitions = 1;
        ui::Dp horizontalPartitionSpacerSize{ 0 };
        int maxVerticalPartitions = 1;
        ui::Dp verticalPartitionSpacerSize{ 0 };
        ui::Dp defaultPanePreferredWidth = DefaultPreferredWidth;
        ui::Dp defaultPanePreferredHeight = DefaultPreferredHeight;
        std::vector<ui::Rect> excludedBounds;   // in window pixels
        bool shouldAutoFocusCurrentDestination = true;

        // equals leaves out shouldAutoFocusCurrentDestination, as Compose's does.
        bool operator==(const PaneScaffoldDirective& o) const {
            return maxHorizontalPartitions == o.maxHorizontalPartitions && horizontalPartitionSpacerSize == o.horizontalPartitionSpacerSize &&
                   maxVerticalPartitions == o.maxVerticalPartitions && verticalPartitionSpacerSize == o.verticalPartitionSpacerSize &&
                   defaultPanePreferredWidth == o.defaultPanePreferredWidth && defaultPanePreferredHeight == o.defaultPanePreferredHeight &&
                   excludedBounds == o.excludedBounds;
        }
        bool IsSinglePaneLayout() const { return maxHorizontalPartitions == 1; }

        static constexpr ui::Dp DefaultPreferredWidth{ 360 }, DefaultPreferredWidthXL{ 412 }, DefaultPreferredHeight{ 420 };
        static PaneScaffoldDirective Default() { return {}; }
    };

    // calculatePaneScaffoldDirective: one pane on compact and medium widths, two on expanded,
    // three beyond (24 dp apart); two stacked panes when tabletop, or one pane on an expanded
    // height. The hinges the policy avoids are excluded.
    PaneScaffoldDirective CalculatePaneScaffoldDirective(const WindowAdaptiveInfo& windowAdaptiveInfo,
                                                         HingePolicy verticalHingePolicy = HingePolicy::AvoidSeparating);
    // ...WithTwoPanesOnMediumWidth: two side by side on medium widths too.
    PaneScaffoldDirective CalculatePaneScaffoldDirectiveWithTwoPanesOnMediumWidth(const WindowAdaptiveInfo& windowAdaptiveInfo,
                                                                                 HingePolicy verticalHingePolicy = HingePolicy::AvoidSeparating);

    class DragToResizeState;
    // A levitated pane's scrim (identity matters: the same function object is the same scrim).
    using ScrimContent = std::shared_ptr<const std::function<void()>>;

    // AdaptStrategy: what a pane does when it does not fit: Hide, Reflow (below another pane,
    // when panes stack), or Levitate (over the others, at an alignment, with a scrim; resizable
    // by dragging).
    struct AdaptStrategy {
        enum class Kind : std::uint8_t { Hide, Reflow, Levitate };
        Kind kind = Kind::Hide;
        ThreePaneScaffoldRole reflowUnder = ThreePaneScaffoldRole::Primary;
        ui::Alignment alignment = ui::Alignment::Center();
        ScrimContent scrim;
        std::shared_ptr<DragToResizeState> dragToResizeState;

        static AdaptStrategy Hide() { return {}; }
        static AdaptStrategy Reflow(ThreePaneScaffoldRole reflowUnder) { return { Kind::Reflow, reflowUnder }; }
        static AdaptStrategy Levitate(ui::Alignment alignment = ui::Alignment::Center(), ScrimContent scrim = nullptr,
                                      std::shared_ptr<DragToResizeState> dragToResizeState = nullptr) {
            return { Kind::Levitate, ThreePaneScaffoldRole::Primary, alignment, std::move(scrim), std::move(dragToResizeState) };
        }
        // Levitate.onlyIf / onlyIfSinglePane: this levitation, or Hide.
        AdaptStrategy OnlyIf(bool condition) const { return condition ? *this : Hide(); }
        AdaptStrategy OnlyIfSinglePane(const PaneScaffoldDirective& d) const { return OnlyIf(d.IsSinglePaneLayout()); }
        bool operator==(const AdaptStrategy& o) const {
            if (kind != o.kind) return false;
            if (kind == Kind::Reflow) return reflowUnder == o.reflowUnder;
            if (kind == Kind::Levitate) return alignment == o.alignment && scrim == o.scrim && dragToResizeState == o.dragToResizeState;
            return true;
        }
    };

    struct ThreePaneScaffoldAdaptStrategies {
        AdaptStrategy primary, secondary, tertiary;
        const AdaptStrategy& operator[](ThreePaneScaffoldRole role) const {
            return role == ThreePaneScaffoldRole::Primary ? primary : role == ThreePaneScaffoldRole::Secondary ? secondary : tertiary;
        }
        bool operator==(const ThreePaneScaffoldAdaptStrategies&) const = default;
    };

    // PaneAdaptedValue: how a pane ended up: Expanded, Hidden, Reflowed (under a pane) or
    // Levitated.
    struct PaneAdaptedValue {
        enum class Kind : std::uint8_t { Expanded, Hidden, Reflowed, Levitated };
        Kind kind = Kind::Hidden;
        ThreePaneScaffoldRole reflowUnder = ThreePaneScaffoldRole::Primary;
        ui::Alignment alignment = ui::Alignment::Center();
        ScrimContent scrim;
        std::shared_ptr<DragToResizeState> dragToResizeState;

        static PaneAdaptedValue Expanded() { return { Kind::Expanded }; }
        static PaneAdaptedValue Hidden() { return { Kind::Hidden }; }
        static PaneAdaptedValue Reflowed(ThreePaneScaffoldRole reflowUnder) { return { Kind::Reflowed, reflowUnder }; }
        static PaneAdaptedValue Levitated(ui::Alignment alignment, ScrimContent scrim = nullptr, std::shared_ptr<DragToResizeState> dragToResizeState = nullptr) {
            return { Kind::Levitated, ThreePaneScaffoldRole::Primary, alignment, std::move(scrim), std::move(dragToResizeState) };
        }
        bool operator==(const PaneAdaptedValue& o) const {
            if (kind != o.kind) return false;
            if (kind == Kind::Reflowed) return reflowUnder == o.reflowUnder;
            if (kind == Kind::Levitated) return alignment == o.alignment && scrim == o.scrim && dragToResizeState == o.dragToResizeState;
            return true;
        }
    };

    // ThreePaneScaffoldHorizontalOrder: the panes left to right (for Ltr; mirrored for Rtl).
    struct ThreePaneScaffoldHorizontalOrder {
        ThreePaneScaffoldRole firstPane, secondPane, thirdPane;   // distinct
        static constexpr int size = 3;
        ThreePaneScaffoldRole operator[](int index) const { return index == 0 ? firstPane : index == 1 ? secondPane : thirdPane; }
        int IndexOf(ThreePaneScaffoldRole role) const { return role == firstPane ? 0 : role == secondPane ? 1 : role == thirdPane ? 2 : -1; }
        ThreePaneScaffoldHorizontalOrder ToLtrOrder(ui::LayoutDirection d) const {
            return d == ui::LayoutDirection::Rtl ? ThreePaneScaffoldHorizontalOrder{ thirdPane, secondPane, firstPane } : *this;
        }
        bool operator==(const ThreePaneScaffoldHorizontalOrder&) const = default;
    };

    // The two expanded panes of a two-pane layout (PaneExpansionStateKey); none otherwise.
    struct PaneExpansionStateKey {
        std::optional<std::pair<ThreePaneScaffoldRole, ThreePaneScaffoldRole>> expandedPanes;   // unset: Default
        bool operator==(const PaneExpansionStateKey&) const = default;
    };

    struct ThreePaneScaffoldValue {
        PaneAdaptedValue primary, secondary, tertiary;
        std::optional<ThreePaneScaffoldRole> currentDestination;   // internal

        const PaneAdaptedValue& operator[](ThreePaneScaffoldRole role) const {
            return role == ThreePaneScaffoldRole::Primary ? primary : role == ThreePaneScaffoldRole::Secondary ? secondary : tertiary;
        }
        // equals compares the three values only.
        bool operator==(const ThreePaneScaffoldValue& o) const { return primary == o.primary && secondary == o.secondary && tertiary == o.tertiary; }
        int ExpandedCount() const;
        PaneExpansionStateKey ExpansionStateKey() const;
        // isInteractable: a hidden pane is not; a levitated one is; the rest are unless a pane
        // levitates with a scrim.
        bool IsInteractable(ThreePaneScaffoldRole role) const;
        void ForEach(const std::function<void(ThreePaneScaffoldRole, const PaneAdaptedValue&)>& action) const {
            action(ThreePaneScaffoldRole::Primary, primary);
            action(ThreePaneScaffoldRole::Secondary, secondary);
            action(ThreePaneScaffoldRole::Tertiary, tertiary);
        }
    };

    // calculateThreePaneScaffoldValue: the current destination levitates if its strategy says
    // so; then panes expand by priority (the destination history, latest first, then primary,
    // secondary, tertiary) while partitions remain; with stacked panes a pane can reflow under
    // its anchor instead. The history holds the destinations' panes, oldest first.
    ThreePaneScaffoldValue CalculateThreePaneScaffoldValue(int maxHorizontalPartitions, const ThreePaneScaffoldAdaptStrategies& adaptStrategies,
                                                           const std::vector<ThreePaneScaffoldRole>& destinationHistory, int maxVerticalPartitions = 1);
    inline ThreePaneScaffoldValue CalculateThreePaneScaffoldValue(int maxHorizontalPartitions, const ThreePaneScaffoldAdaptStrategies& adaptStrategies,
                                                                  std::optional<ThreePaneScaffoldRole> currentDestination, int maxVerticalPartitions = 1) {
        std::vector<ThreePaneScaffoldRole> history;
        if (currentDestination) history.push_back(*currentDestination);
        return CalculateThreePaneScaffoldValue(maxHorizontalPartitions, adaptStrategies, history, maxVerticalPartitions);
    }

    // ThreePaneScaffoldDestinationItem: a pane and the content it shows.
    template<typename T>
    struct ThreePaneScaffoldDestinationItem {
        ThreePaneScaffoldRole pane;
        std::optional<T> contentKey;
        bool operator==(const ThreePaneScaffoldDestinationItem&) const = default;
    };

}
