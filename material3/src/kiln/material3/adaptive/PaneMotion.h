#pragma once

// adaptive-layout PaneMotion.kt and ThreePaneMotion.kt: how each pane of a three-pane scaffold
// moves when the scaffold value changes. Panes that stay shown animate their bounds; panes that
// appear or disappear slide in or out (from the side nothing blocks), expand or shrink between
// panes that stay, or fade as modals when levitated. The motion data provider holds each pane's
// motion and its origin and target bounds, from which the default transitions take their
// offsets.

#include "kiln/animation/Spec.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/material3/adaptive/PaneScaffoldValue.h"
#include "kiln/ui/Geometry.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace kiln::material3::adaptive {

    // PaneMotion: a named motion and its type.
    class PaneMotion {
    public:
        enum class Type : std::uint8_t { Hidden = 0, Exiting = 1, Entering = 2, Shown = 3, ExitingModal = 5, EnteringModal = 6 };
        // Type.calculate: from the pane's previous and current adapted values.
        static Type CalculateType(const PaneAdaptedValue& previous, const PaneAdaptedValue& current);

        Type type() const { return m_Type; }
        std::string_view Name() const { return m_Name; }
        bool operator==(const PaneMotion& o) const { return m_Id == o.m_Id; }

        static const PaneMotion NoMotion, AnimateBounds, EnterFromLeft, EnterFromRight, EnterFromLeftDelayed, EnterFromRightDelayed,
            ExitToLeft, ExitToRight, EnterWithExpand, ExitWithShrink, EnterAsModal, ExitAsModal;

    private:
        constexpr PaneMotion(int id, std::string_view name, Type type) : m_Id(id), m_Name(name), m_Type(type) {}
        int m_Id;
        std::string_view m_Name;
        Type m_Type;
    };

    // ThreePaneMotion: the motion of each pane.
    struct ThreePaneMotion {
        PaneMotion primary = PaneMotion::NoMotion, secondary = PaneMotion::NoMotion, tertiary = PaneMotion::NoMotion;
        const PaneMotion& operator[](ThreePaneScaffoldRole role) const {
            return role == ThreePaneScaffoldRole::Primary ? primary : role == ThreePaneScaffoldRole::Secondary ? secondary : tertiary;
        }
        bool operator==(const ThreePaneMotion&) const = default;
    };

    // calculatePaneMotion: the motion of each pane, in `paneOrder`'s order.
    std::array<PaneMotion, 3> CalculatePaneMotion(const ThreePaneScaffoldValue& previous, const ThreePaneScaffoldValue& current,
                                                  const ThreePaneScaffoldHorizontalOrder& paneOrder);

    // PaneMotionData: a pane's motion, and its bounds before (origin) and after (target) it.
    struct PaneMotionData {
        PaneMotion motion = PaneMotion::NoMotion;
        ui::IntSize originSize, targetSize;
        ui::IntOffset originPosition, targetPosition;
        float zIndex = 0;                       // internal
        bool isOriginSizeAndPositionSet = false, isTargetSizeAndPositionSet = false;

        int TargetLeft() const { return targetPosition.x; }
        int TargetRight() const { return targetPosition.x + targetSize.width; }
        int TargetTop() const { return targetPosition.y; }
        int TargetBottom() const { return targetPosition.y + targetSize.height; }
        int CurrentLeft() const { return originPosition.x; }
        int CurrentRight() const { return originPosition.x + originSize.width; }
    };

    class PredictiveBackScaleState;

    // PaneScaffoldMotionDataProvider (ThreePaneScaffoldMotionDataProvider): the panes' motion
    // data, left to right.
    class ThreePaneScaffoldMotionDataProvider {
    public:
        ThreePaneScaffoldMotionDataProvider();
        ~ThreePaneScaffoldMotionDataProvider();
        ui::IntSize scaffoldSize;
        static constexpr int count = 3;
        ThreePaneScaffoldRole GetRoleAt(int index) const { return m_LtrOrder[index]; }
        PaneMotionData& operator[](ThreePaneScaffoldRole role) { return m_Data[int(role)]; }
        const PaneMotionData& operator[](ThreePaneScaffoldRole role) const { return m_Data[int(role)]; }
        PaneMotionData& At(int index) { return (*this)[GetRoleAt(index)]; }
        const PaneMotionData& At(int index) const { return (*this)[GetRoleAt(index)]; }
        // update: new motions and order; the origins are taken again.
        void Update(const ThreePaneMotion& motion, const ThreePaneScaffoldHorizontalOrder& ltrOrder);

        // The slide distances of the default transitions (left / right of the scaffold).
        int SlideInFromLeftOffset() const;
        int SlideInFromRightOffset() const;
        int SlideOutToLeftOffset() const;
        int SlideOutToRightOffset() const;
        // Where a hidden pane's left edge is now (the right edge of the shown pane before it) /
        // where a hiding pane's left edge ends up.
        int GetHiddenPaneCurrentLeft(ThreePaneScaffoldRole role) const;
        int GetHidingPaneTargetLeft(ThreePaneScaffoldRole role) const;

        // The scaffold's scale during a predictive back gesture.
        PredictiveBackScaleState& predictiveBackScaleState() { return *m_BackScale; }
        std::function<bool()> isPredictiveBackInProgress;   // set by the scaffold

    private:
        ThreePaneScaffoldHorizontalOrder m_LtrOrder{ ThreePaneScaffoldRole::Primary, ThreePaneScaffoldRole::Secondary, ThreePaneScaffoldRole::Tertiary };
        std::array<PaneMotionData, 3> m_Data;
        std::unique_ptr<PredictiveBackScaleState> m_BackScale;
    };

    namespace PaneMotionDefaults {
        // spring(0.8, 380) with a 1 px threshold on every edge.
        anim::SpecPtr AnimationSpec();
        // The same spring after a pause of a tenth of its duration.
        anim::SpecPtr DelayedAnimationSpec();
        anim::SpecPtr VisibilityAnimationSpec();   // spring(0.8, 380)
        // The bounds springs applied to offsets and sizes (DerivedOffset/SizeAnimationSpec).
        anim::SpecPtr OffsetAnimationSpec();
        anim::SpecPtr SizeAnimationSpec();
        anim::SpecPtr DelayedOffsetAnimationSpec();
        anim::SpecPtr DelayedSizeAnimationSpec();
    }

    // DelayedSpringSpec: a spring that holds its initial value for `delayedRatio` of its
    // duration first.
    anim::SpecPtr DelayedSpringSpec(float dampingRatio, float stiffness, float delayedRatio, float visibilityThreshold);

    // calculateDefaultEnterTransition / ExitTransition: the pane's motion as a transition.
    foundation::EnterTransition CalculateDefaultEnterTransition(const ThreePaneScaffoldMotionDataProvider& provider, ThreePaneScaffoldRole role);
    foundation::ExitTransition CalculateDefaultExitTransition(const ThreePaneScaffoldMotionDataProvider& provider, ThreePaneScaffoldRole role);

}
