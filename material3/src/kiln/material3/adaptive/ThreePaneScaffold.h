#pragma once

// adaptive-layout's three-pane scaffolds: ThreePaneScaffold.kt (the layout: expanded panes side
// by side in the partitions the directive allows, split by the pane expansion when it is set,
// reflowed panes under their anchor, levitated panes over the others, hidden ones kept in place
// while they animate away), ThreePaneScaffoldState.kt (the seekable transition between scaffold
// values), the scopes (PaneScaffold.kt, ThreePaneScaffoldScope.kt), Pane.kt (AnimatedPane:
// a pane's enter / exit and bounds animation), PaneMargins.kt, LevitatedPaneScrim.kt,
// PredictiveBackScaleState.kt, the animate-bounds / animate-with-fading approach modifiers and
// the pane expansion drag handle modifier, and the list-detail and supporting-pane scaffolds.

#include "kiln/animation/Animatable.h"
#include <kiln/Function.h>
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/MutatorMutex.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/foundation/Transition.h"
#include "kiln/material3/adaptive/PaneExpansion.h"
#include "kiln/material3/adaptive/PaneMotion.h"
#include "kiln/material3/adaptive/PaneScaffoldValue.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Focus.h"
#include "kiln/ui/Layout.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3::adaptive {

    // --- State ----------------------------------------------------------------------------------

    // ThreePaneScaffoldState / MutableThreePaneScaffoldState: the transition between scaffold
    // values, seekable (a predictive back gesture's progress).
    class ThreePaneScaffoldState {
    public:
        explicit ThreePaneScaffoldState(ThreePaneScaffoldValue initialScaffoldValue) : m_Transition(std::move(initialScaffoldValue)) {}
        ThreePaneScaffoldValue CurrentState() const { return m_Transition.CurrentState(); }
        ThreePaneScaffoldValue TargetState() const { return m_Transition.TargetState(); }
        // The progress from the current to the target state (0 when they are the same).
        float ProgressFraction() const { return m_Transition.Fraction(); }
        bool IsPredictiveBackInProgress() const { return m_PredictiveBack.Get(); }

        rt::Task<> SnapTo(ThreePaneScaffoldValue targetState);
        rt::Task<> SeekTo(float fraction, std::optional<ThreePaneScaffoldValue> targetState = std::nullopt, bool isPredictiveBackInProgress = false);
        // animateTo(targetState, animationSpec): null spec traverses the transition linearly.
        rt::Task<> AnimateTo(std::optional<ThreePaneScaffoldValue> targetState = std::nullopt, anim::SpecPtr animationSpec = nullptr,
                             bool isPredictiveBackInProgress = false);
        // rememberTransition
        foundation::Transition<ThreePaneScaffoldValue>& RememberTransition(rt::Key key = {});

    private:
        foundation::SeekableTransitionState<ThreePaneScaffoldValue> m_Transition;
        rt::State<bool> m_PredictiveBack{ false };
        std::uint64_t m_Mutation = 0;
    };
    using MutableThreePaneScaffoldState = ThreePaneScaffoldState;

    // PredictiveBackScaleState: the scaffold shrinks (to 95% at most) while a predictive back
    // gesture is in progress; offsets convert into the scaled space.
    class PredictiveBackScaleState {
    public:
        PredictiveBackScaleState(kiln::Function<ui::IntSize()> scaffoldSize, kiln::Function<bool()> isPredictiveBackInProgress)
            : m_ScaffoldSize(std::move(scaffoldSize)), m_InProgress(std::move(isPredictiveBackInProgress)) {}
        anim::Animatable scaleAnimatable{ 1.0f };
        float Scale() const { return scaleAnimatable.Float(); }
        ui::IntOffset Convert(ui::IntOffset offset) const;
        static constexpr float PredictiveBackMinScale = 0.95f;
    private:
        kiln::Function<ui::IntSize()> m_ScaffoldSize;
        kiln::Function<bool()> m_InProgress;
    };

    // --- Pane margins and parent data -------------------------------------------------------------

    // PaneMargins: where a pane's edges may go: at least the fixed margins and the insets (its
    // rulers) from the scaffold's edges.
    struct PaneMargins {
        bool specified = false;
        int fixedLeft = 0, fixedTop = 0, fixedRight = 0, fixedBottom = 0;   // px
        std::vector<ui::RectRulers> insets;
        int GetPaneLeft(ui::PlacementScope& s, int measuredLeft) const;
        int GetPaneTop(ui::PlacementScope& s, int measuredTop) const;
        int GetPaneRight(ui::PlacementScope& s, int measuredRight, int parentRight) const;
        int GetPaneBottom(ui::PlacementScope& s, int measuredBottom, int parentBottom) const;
    };

    // PaneScaffoldParentData: what a pane tells the scaffold (in ParentData::extra).
    struct PaneScaffoldParentData {
        std::optional<ui::Dp> preferredWidth, preferredHeight;
        float preferredWidthInProportion = NAN, preferredHeightInProportion = NAN;
        PaneMargins paneMargins;
        bool isAnimatedPane = false;
        std::optional<ui::Dp> minTouchTargetSize;
    };
    const PaneScaffoldParentData* PaneScaffoldParentDataOf(const ui::Measurable& m);

    // --- Scopes ---------------------------------------------------------------------------------

    class AnimateBoundsLookahead;

    // ThreePaneScaffoldScope: the scaffold's scope (PaneScaffoldScope, the transition scope and
    // its lookahead scope) for the panes and the drag handle.
    class ThreePaneScaffoldScope : public ui::LookaheadScope {
    public:
        // Modifier.preferredWidth / preferredHeight: a pane's size in dp or as a proportion of
        // the scaffold.
        ui::Modifier PreferredWidth(ui::Modifier m, ui::Dp width) const;
        ui::Modifier PreferredWidth(ui::Modifier m, float proportion) const;
        ui::Modifier PreferredHeight(ui::Modifier m, ui::Dp height) const;
        ui::Modifier PreferredHeight(ui::Modifier m, float proportion) const;
        // Modifier.paneMargins(fixedMargins, insets).
        ui::Modifier PaneMarginsMod(ui::Modifier m, foundation::PaddingValues fixedMargins, std::vector<ui::RectRulers> insets = {}) const;
        // Modifier.paneExpansionDraggable: makes the drag handle drag the expansion; null
        // semantics: the default ones (the current anchor and an action to the next).
        ui::Modifier PaneExpansionDraggable(ui::Modifier m, PaneExpansionState& state, ui::Dp minTouchTargetSize,
                                            std::shared_ptr<foundation::MutableInteractionSource> interactionSource,
                                            kiln::Function<void(ui::SemanticsConfiguration&)> semanticsProperties = nullptr) const;
        rt::SaveableStateHolder& SaveableStateHolder() const { return *m_Holder; }
        // PaneScaffoldTransitionScope
        foundation::Transition<ThreePaneScaffoldValue>& ScaffoldStateTransition() const { return *m_Transition; }
        float MotionProgress() const;
        ThreePaneScaffoldMotionDataProvider& MotionDataProvider() const { return *m_Motion; }
        ui::FocusRequester& FocusRequesterOf(ThreePaneScaffoldRole role) const { return (*m_FocusRequesters)[std::size_t(role)]; }
        // LookaheadScope
        ui::LayoutCoordinates LookaheadScopeCoordinates(const ui::PlacementScope& scope) const override {
            return m_Lookahead->LookaheadScopeCoordinates(scope);
        }

        // internal
        foundation::Transition<ThreePaneScaffoldValue>* m_Transition = nullptr;
        ThreePaneScaffoldState* m_State = nullptr;
        ThreePaneScaffoldMotionDataProvider* m_Motion = nullptr;
        ui::LookaheadScope* m_Lookahead = nullptr;
        rt::SaveableStateHolder* m_Holder = nullptr;
        std::array<ui::FocusRequester, 3>* m_FocusRequesters = nullptr;
    };

    // ThreePaneScaffoldPaneScope: a pane's scope (its role, motion and whether it takes input).
    class ThreePaneScaffoldPaneScope : public ThreePaneScaffoldScope {
    public:
        ThreePaneScaffoldRole paneRole = ThreePaneScaffoldRole::Primary;
        PaneMotion paneMotion = PaneMotion::ExitToLeft;
        bool isInteractable = false;
    };

    // --- AnimatedPane ---------------------------------------------------------------------------

    struct AnimatedPaneOptions {
        std::optional<foundation::EnterTransition> enterTransition;   // unset: the pane motion's default
        std::optional<foundation::ExitTransition> exitTransition;
        anim::SpecPtr boundsAnimationSpec;                             // null: PaneMotionDefaults::AnimationSpec()
        // A handle shown above the content of a levitated pane with a DragToResizeState.
        kiln::Function<void(DragToResizeState&)> dragToResizeHandle;
        foundation::ShapePtr shape;                                    // null: RectangleShape
    };
    // AnimatedPane: a pane that animates in and out with its pane motion, animates its bounds
    // while it stays shown, and (levitated) casts a shadow over a scrim.
    void AnimatedPane(const ThreePaneScaffoldPaneScope& scope, const ui::Modifier& modifier, const AnimatedPaneOptions& options,
                      kiln::Function<void()> content);
    inline void AnimatedPane(const ThreePaneScaffoldPaneScope& scope, kiln::Function<void()> content) {
        AnimatedPane(scope, {}, {}, std::move(content));
    }
    namespace AnimatedPaneDefaults { inline constexpr ui::Dp ShadowElevation{ 15 }; }

    // LevitatedPaneScrim: a scrim over the panes under a levitated one (32% black); a click on it
    // runs `onClick` (without it, it is hidden from accessibility).
    void LevitatedPaneScrim(const ui::Modifier& modifier = {}, kiln::Function<void()> onClick = nullptr,
                            std::optional<ui::Color> color = std::nullopt);
    namespace LevitatedPaneScrimDefaults { inline const ui::Color Color = ui::Color::Black().WithAlpha(0.32f); }

    // --- The scaffolds --------------------------------------------------------------------------

    using PaneContent = kiln::Function<void(const ThreePaneScaffoldPaneScope&)>;
    using DragHandleContent = kiln::Function<void(const ThreePaneScaffoldScope&, PaneExpansionState&)>;

    namespace ThreePaneScaffoldDefaults {
        inline constexpr int PrimaryPanePriority = 10, SecondaryPanePriority = 5, TertiaryPanePriority = 1;
        inline constexpr float LevitatedPaneZIndex = 1, HiddenPaneZIndexOffset = -0.1f;
        inline constexpr ui::Dp MinPaneWidth{ 48 }, MinPaneHeight{ 48 };
    }

    struct ThreePaneScaffoldOptions {
        PaneContent tertiaryPane;
        PaneExpansionState* paneExpansionState = nullptr;   // null: the default one
        DragHandleContent paneExpansionDragHandle;
    };
    // ThreePaneScaffold(scaffoldState, paneOrder, ...): the panes laid out by the state's target
    // value, animating between values.
    void ThreePaneScaffold(const ui::Modifier& modifier, const PaneScaffoldDirective& scaffoldDirective, ThreePaneScaffoldState& scaffoldState,
                           const ThreePaneScaffoldHorizontalOrder& paneOrder, PaneContent secondaryPane, PaneContent primaryPane,
                           const ThreePaneScaffoldOptions& options = {});
    // ThreePaneScaffold(scaffoldValue, ...): a remembered state animated to each new value.
    void ThreePaneScaffold(const ui::Modifier& modifier, const PaneScaffoldDirective& scaffoldDirective, const ThreePaneScaffoldValue& scaffoldValue,
                           const ThreePaneScaffoldHorizontalOrder& paneOrder, PaneContent secondaryPane, PaneContent primaryPane,
                           const ThreePaneScaffoldOptions& options = {});

    namespace ListDetailPaneScaffoldRole {
        inline constexpr ThreePaneScaffoldRole List = ThreePaneScaffoldRole::Secondary;
        inline constexpr ThreePaneScaffoldRole Detail = ThreePaneScaffoldRole::Primary;
        inline constexpr ThreePaneScaffoldRole Extra = ThreePaneScaffoldRole::Tertiary;
    }
    namespace ListDetailPaneScaffoldDefaults {
        ThreePaneScaffoldAdaptStrategies adaptStrategies(AdaptStrategy detailPaneAdaptStrategy = AdaptStrategy::Hide(),
                                                         AdaptStrategy listPaneAdaptStrategy = AdaptStrategy::Hide(),
                                                         AdaptStrategy extraPaneAdaptStrategy = AdaptStrategy::Hide());
        inline constexpr ThreePaneScaffoldHorizontalOrder PaneOrder{ ThreePaneScaffoldRole::Secondary, ThreePaneScaffoldRole::Primary,
                                                                     ThreePaneScaffoldRole::Tertiary };
    }
    struct ListDetailPaneScaffoldOptions {
        PaneContent extraPane;
        DragHandleContent paneExpansionDragHandle;
        PaneExpansionState* paneExpansionState = nullptr;
    };
    // ListDetailPaneScaffold: a list (secondary) and its details (primary), with an optional
    // extra pane, filling the available space.
    void ListDetailPaneScaffold(const PaneScaffoldDirective& directive, ThreePaneScaffoldState& scaffoldState, PaneContent listPane,
                                PaneContent detailPane, const ui::Modifier& modifier = {}, const ListDetailPaneScaffoldOptions& options = {});
    void ListDetailPaneScaffold(const PaneScaffoldDirective& directive, const ThreePaneScaffoldValue& value, PaneContent listPane,
                                PaneContent detailPane, const ui::Modifier& modifier = {}, const ListDetailPaneScaffoldOptions& options = {});

    namespace SupportingPaneScaffoldRole {
        inline constexpr ThreePaneScaffoldRole Main = ThreePaneScaffoldRole::Primary;
        inline constexpr ThreePaneScaffoldRole Supporting = ThreePaneScaffoldRole::Secondary;
        inline constexpr ThreePaneScaffoldRole Extra = ThreePaneScaffoldRole::Tertiary;
    }
    namespace SupportingPaneScaffoldDefaults {
        ThreePaneScaffoldAdaptStrategies adaptStrategies(AdaptStrategy mainPaneAdaptStrategy = AdaptStrategy::Hide(),
                                                         AdaptStrategy supportingPaneAdaptStrategy = AdaptStrategy::Reflow(SupportingPaneScaffoldRole::Main),
                                                         AdaptStrategy extraPaneAdaptStrategy = AdaptStrategy::Hide());
        inline constexpr ThreePaneScaffoldHorizontalOrder PaneOrder{ ThreePaneScaffoldRole::Primary, ThreePaneScaffoldRole::Secondary,
                                                                     ThreePaneScaffoldRole::Tertiary };
    }
    using SupportingPaneScaffoldOptions = ListDetailPaneScaffoldOptions;
    // SupportingPaneScaffold: main content (primary) with supporting content beside it.
    void SupportingPaneScaffold(const PaneScaffoldDirective& directive, ThreePaneScaffoldState& scaffoldState, PaneContent mainPane,
                                PaneContent supportingPane, const ui::Modifier& modifier = {}, const SupportingPaneScaffoldOptions& options = {});
    void SupportingPaneScaffold(const PaneScaffoldDirective& directive, const ThreePaneScaffoldValue& value, PaneContent mainPane,
                                PaneContent supportingPane, const ui::Modifier& modifier = {}, const SupportingPaneScaffoldOptions& options = {});

}
