#include "kiln/material3/adaptive/PaneMotion.h"

#include "kiln/material3/adaptive/ThreePaneScaffold.h"

#include <algorithm>

namespace kiln::material3::adaptive {

    using foundation::EnterTransition;
    using foundation::ExitTransition;
    using T = PaneMotion::Type;

    const PaneMotion PaneMotion::NoMotion{ 0, "NoMotion", T::Hidden };
    const PaneMotion PaneMotion::AnimateBounds{ 1, "AnimateBounds", T::Shown };
    const PaneMotion PaneMotion::EnterFromLeft{ 2, "EnterFromLeft", T::Entering };
    const PaneMotion PaneMotion::EnterFromRight{ 3, "EnterFromRight", T::Entering };
    const PaneMotion PaneMotion::EnterFromLeftDelayed{ 4, "EnterFromLeftDelayed", T::Entering };
    const PaneMotion PaneMotion::EnterFromRightDelayed{ 5, "EnterFromRightDelayed", T::Entering };
    const PaneMotion PaneMotion::ExitToLeft{ 6, "ExitToLeft", T::Exiting };
    const PaneMotion PaneMotion::ExitToRight{ 7, "ExitToRight", T::Exiting };
    const PaneMotion PaneMotion::EnterWithExpand{ 8, "EnterWithExpand", T::Entering };
    const PaneMotion PaneMotion::ExitWithShrink{ 9, "ExitWithShrink", T::Exiting };
    const PaneMotion PaneMotion::EnterAsModal{ 10, "EnterAsModal", T::EnteringModal };
    const PaneMotion PaneMotion::ExitAsModal{ 11, "ExitAsModal", T::ExitingModal };

    PaneMotion::Type PaneMotion::CalculateType(const PaneAdaptedValue& previous, const PaneAdaptedValue& current) {
        const bool wasShown = previous.kind != PaneAdaptedValue::Kind::Hidden;
        const bool isShown = current.kind != PaneAdaptedValue::Kind::Hidden;
        const bool levitated = previous.kind == PaneAdaptedValue::Kind::Levitated || current.kind == PaneAdaptedValue::Kind::Levitated;
        if (wasShown && isShown) return T::Shown;
        if (!wasShown && !isShown) return T::Hidden;
        if (wasShown) return levitated ? T::ExitingModal : T::Exiting;
        return levitated ? T::EnteringModal : T::Entering;
    }

    std::array<PaneMotion, 3> CalculatePaneMotion(const ThreePaneScaffoldValue& previous, const ThreePaneScaffoldValue& current,
                                                  const ThreePaneScaffoldHorizontalOrder& order) {
        constexpr int n = 3;
        std::array<T, 3> types{};
        std::array<PaneMotion, 3> motions{ PaneMotion::NoMotion, PaneMotion::NoMotion, PaneMotion::NoMotion };
        int firstShown = n, firstEntering = n, lastShown = -1, lastEntering = -1;
        // First pass: entering / exiting, and the panes that keep showing animate their bounds.
        for (int i = 0; i < n; ++i) {
            types[i] = PaneMotion::CalculateType(previous[order[i]], current[order[i]]);
            if (types[i] == T::Shown) {
                firstShown = std::min(firstShown, i);
                lastShown = std::max(lastShown, i);
                motions[i] = PaneMotion::AnimateBounds;
            } else if (types[i] == T::Entering) {
                firstEntering = std::min(firstEntering, i);
                lastEntering = std::max(lastEntering, i);
            }
        }
        // Second pass: exits, to the side nothing (entering or shown) blocks.
        bool exitToRight = false, exitToLeft = false;
        int firstExitToRight = n, lastExitToLeft = -1;
        for (int i = 0; i < n; ++i) {
            if (types[i] != T::Exiting) continue;
            const bool shownLeft = firstShown < i, enteringLeft = firstEntering < i;
            const bool shownRight = lastShown > i, enteringRight = lastEntering > i;
            if (!shownRight && !enteringRight) {
                exitToRight = true; firstExitToRight = std::min(firstExitToRight, i); motions[i] = PaneMotion::ExitToRight;
            } else if (!shownLeft && !enteringLeft) {
                exitToLeft = true; lastExitToLeft = std::max(lastExitToLeft, i); motions[i] = PaneMotion::ExitToLeft;
            } else if (!shownRight) {
                exitToRight = true; firstExitToRight = std::min(firstExitToRight, i); motions[i] = PaneMotion::ExitToRight;
            } else if (!shownLeft) {
                exitToLeft = true; lastExitToLeft = std::max(lastExitToLeft, i); motions[i] = PaneMotion::ExitToLeft;
            } else {
                motions[i] = PaneMotion::ExitWithShrink;
            }
        }
        // Third pass: entrances. A pane shown on a side, or one there exiting towards this pane,
        // blocks that side.
        for (int i = 0; i < n; ++i) {
            if (types[i] != T::Entering) continue;
            const bool noBlockingRight = !(lastShown > i) && !(lastExitToLeft > i);
            const bool noBlockingLeft = !(firstShown < i) && !(firstExitToRight < i);
            if (noBlockingRight && !exitToRight) motions[i] = PaneMotion::EnterFromRight;
            else if (noBlockingLeft && !exitToLeft) motions[i] = PaneMotion::EnterFromLeft;
            else if (noBlockingRight) motions[i] = PaneMotion::EnterFromRightDelayed;
            else if (noBlockingLeft) motions[i] = PaneMotion::EnterFromLeftDelayed;
            else motions[i] = PaneMotion::EnterWithExpand;
        }
        // Fourth pass: levitated panes.
        for (int i = 0; i < n; ++i) {
            if (types[i] == T::EnteringModal) motions[i] = PaneMotion::EnterAsModal;
            else if (types[i] == T::ExitingModal) motions[i] = PaneMotion::ExitAsModal;
        }
        return motions;
    }

    // --- Motion data ----------------------------------------------------------------------------

    ThreePaneScaffoldMotionDataProvider::ThreePaneScaffoldMotionDataProvider()
        : m_BackScale(std::make_unique<PredictiveBackScaleState>([this] { return scaffoldSize; },
                                                                 [this] { return isPredictiveBackInProgress && isPredictiveBackInProgress(); })) {}
    ThreePaneScaffoldMotionDataProvider::~ThreePaneScaffoldMotionDataProvider() = default;

    void ThreePaneScaffoldMotionDataProvider::Update(const ThreePaneMotion& motion, const ThreePaneScaffoldHorizontalOrder& ltrOrder) {
        m_LtrOrder = ltrOrder;
        for (ThreePaneScaffoldRole role : { ThreePaneScaffoldRole::Primary, ThreePaneScaffoldRole::Secondary, ThreePaneScaffoldRole::Tertiary }) {
            PaneMotionData& d = (*this)[role];
            d.motion = motion[role];
            d.isOriginSizeAndPositionSet = false;
        }
    }

    namespace {
        bool ShownOrEntering(const PaneMotionData& d) { return d.motion.type() == T::Shown || d.motion.type() == T::Entering; }
        bool ShownOrExiting(const PaneMotionData& d) { return d.motion.type() == T::Shown || d.motion.type() == T::Exiting; }
    }

    // From the left: the target left edge of the pane after the panes sliding in from the left
    // (keeping the spacer), else the right edge of the last one sliding in.
    int ThreePaneScaffoldMotionDataProvider::SlideInFromLeftOffset() const {
        const PaneMotionData* previous = nullptr;
        for (int i = count - 1; i >= 0; --i) {
            const PaneMotionData& d = At(i);
            if (d.motion == PaneMotion::EnterFromLeft || d.motion == PaneMotion::EnterFromLeftDelayed)
                return -(previous ? previous->TargetLeft() : d.TargetRight());
            if (ShownOrEntering(d)) previous = &d;
        }
        return 0;
    }

    int ThreePaneScaffoldMotionDataProvider::SlideInFromRightOffset() const {
        const PaneMotionData* previous = nullptr;
        for (int i = 0; i < count; ++i) {
            const PaneMotionData& d = At(i);
            if (d.motion == PaneMotion::EnterFromRight || d.motion == PaneMotion::EnterFromRightDelayed)
                return scaffoldSize.width - (previous ? previous->TargetRight() : d.TargetLeft());
            if (ShownOrEntering(d)) previous = &d;
        }
        return 0;
    }

    int ThreePaneScaffoldMotionDataProvider::SlideOutToLeftOffset() const {
        const PaneMotionData* previous = nullptr;
        for (int i = count - 1; i >= 0; --i) {
            const PaneMotionData& d = At(i);
            if (d.motion == PaneMotion::ExitToLeft) return -(previous ? previous->CurrentLeft() : d.CurrentRight());
            if (ShownOrExiting(d)) previous = &d;
        }
        return 0;
    }

    int ThreePaneScaffoldMotionDataProvider::SlideOutToRightOffset() const {
        const PaneMotionData* previous = nullptr;
        for (int i = 0; i < count; ++i) {
            const PaneMotionData& d = At(i);
            if (d.motion == PaneMotion::ExitToRight) return scaffoldSize.width - (previous ? previous->CurrentRight() : d.CurrentLeft());
            if (ShownOrExiting(d)) previous = &d;
        }
        return 0;
    }

    int ThreePaneScaffoldMotionDataProvider::GetHiddenPaneCurrentLeft(ThreePaneScaffoldRole role) const {
        int left = 0;
        for (int i = 0; i < count; ++i) {
            if (GetRoleAt(i) == role) return left;
            if (ShownOrExiting(At(i))) left = At(i).CurrentRight();
        }
        return left;
    }

    int ThreePaneScaffoldMotionDataProvider::GetHidingPaneTargetLeft(ThreePaneScaffoldRole role) const {
        int left = 0;
        for (int i = 0; i < count; ++i) {
            if (GetRoleAt(i) == role) return left;
            if (ShownOrEntering(At(i))) left = At(i).TargetRight();
        }
        return left;
    }

    // --- Specs ----------------------------------------------------------------------------------

    namespace {
        class DelayedSpring final : public anim::AnimationSpec {
        public:
            DelayedSpring(anim::SpecPtr spring, float ratio) : m_Spring(std::move(spring)), m_Ratio(ratio) {}
            anim::Vec ValueAt(std::int64_t t, const anim::Vec& from, const anim::Vec& to, const anim::Vec& v0, int dims) const override {
                const std::int64_t delay = Delay(from, to, v0, dims);
                return t <= delay ? from : m_Spring->ValueAt(t - delay, from, to, v0, dims);
            }
            anim::Vec VelocityAt(std::int64_t t, const anim::Vec& from, const anim::Vec& to, const anim::Vec& v0, int dims) const override {
                const std::int64_t delay = Delay(from, to, v0, dims);
                return t <= delay ? v0 : m_Spring->VelocityAt(t - delay, from, to, v0, dims);
            }
            std::int64_t DurationNanos(const anim::Vec& from, const anim::Vec& to, const anim::Vec& v0, int dims) const override {
                const std::int64_t d = m_Spring->DurationNanos(from, to, v0, dims);
                return d + std::int64_t(double(d) * m_Ratio);
            }
        private:
            std::int64_t Delay(const anim::Vec& from, const anim::Vec& to, const anim::Vec& v0, int dims) const {
                return std::int64_t(double(m_Spring->DurationNanos(from, to, v0, dims)) * m_Ratio);
            }
            anim::SpecPtr m_Spring;
            float m_Ratio;
        };
    }

    anim::SpecPtr DelayedSpringSpec(float dampingRatio, float stiffness, float delayedRatio, float visibilityThreshold) {
        return std::make_shared<DelayedSpring>(anim::Spring(dampingRatio, stiffness, visibilityThreshold), delayedRatio);
    }

    namespace PaneMotionDefaults {
        // An IntRect threshold of 1 on every edge: the derived offset and size specs are the
        // same spring with a 1 px threshold on their components.
        anim::SpecPtr AnimationSpec() { static const auto s = anim::Spring(0.8f, 380, 1); return s; }
        anim::SpecPtr DelayedAnimationSpec() { static const auto s = DelayedSpringSpec(0.8f, 380, 0.1f, 1); return s; }
        anim::SpecPtr VisibilityAnimationSpec() { static const auto s = anim::Spring(0.8f, 380); return s; }
        anim::SpecPtr OffsetAnimationSpec() { return AnimationSpec(); }
        anim::SpecPtr SizeAnimationSpec() { return AnimationSpec(); }
        anim::SpecPtr DelayedOffsetAnimationSpec() { return DelayedAnimationSpec(); }
        anim::SpecPtr DelayedSizeAnimationSpec() { return DelayedAnimationSpec(); }
    }

    EnterTransition CalculateDefaultEnterTransition(const ThreePaneScaffoldMotionDataProvider& provider, ThreePaneScaffoldRole role) {
        const auto* p = &provider;
        const PaneMotion& m = provider[role].motion;
        using namespace PaneMotionDefaults;
        if (m == PaneMotion::EnterFromLeft) return foundation::SlideInHorizontally([p](int) { return p->SlideInFromLeftOffset(); }, OffsetAnimationSpec());
        if (m == PaneMotion::EnterFromLeftDelayed)
            return foundation::SlideInHorizontally([p](int) { return p->SlideInFromLeftOffset(); }, DelayedOffsetAnimationSpec());
        if (m == PaneMotion::EnterFromRight) return foundation::SlideInHorizontally([p](int) { return p->SlideInFromRightOffset(); }, OffsetAnimationSpec());
        if (m == PaneMotion::EnterFromRightDelayed)
            return foundation::SlideInHorizontally([p](int) { return p->SlideInFromRightOffset(); }, DelayedOffsetAnimationSpec());
        if (m == PaneMotion::EnterWithExpand)
            return foundation::ExpandHorizontally(SizeAnimationSpec(), ui::AlignmentH::CenterHorizontally()) +
                   foundation::SlideInHorizontally([p, role](int) { return p->GetHiddenPaneCurrentLeft(role) - (*p)[role].TargetLeft(); },
                                                   OffsetAnimationSpec());
        if (m == PaneMotion::EnterAsModal) return foundation::FadeIn(VisibilityAnimationSpec());
        return EnterTransition{};
    }

    ExitTransition CalculateDefaultExitTransition(const ThreePaneScaffoldMotionDataProvider& provider, ThreePaneScaffoldRole role) {
        const auto* p = &provider;
        const PaneMotion& m = provider[role].motion;
        using namespace PaneMotionDefaults;
        if (m == PaneMotion::ExitToLeft) return foundation::SlideOutHorizontally([p](int) { return p->SlideOutToLeftOffset(); }, OffsetAnimationSpec());
        if (m == PaneMotion::ExitToRight) return foundation::SlideOutHorizontally([p](int) { return p->SlideOutToRightOffset(); }, OffsetAnimationSpec());
        if (m == PaneMotion::ExitWithShrink)
            return foundation::ShrinkHorizontally(SizeAnimationSpec(), ui::AlignmentH::CenterHorizontally()) +
                   foundation::SlideOutHorizontally([p, role](int) { return p->GetHidingPaneTargetLeft(role) - (*p)[role].CurrentLeft(); },
                                                    OffsetAnimationSpec());
        if (m == PaneMotion::ExitAsModal) return foundation::FadeOut(VisibilityAnimationSpec());
        return ExitTransition{};
    }

}
