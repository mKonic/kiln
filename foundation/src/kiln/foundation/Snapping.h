#pragma once

// Snapping (gestures/snapping): a fling that approaches with a decay (or a spring when the
// decay would not get there) and then snaps to a position the layout picks
// (SnapLayoutInfoProvider). rememberSnapFlingBehavior(lazyListState) snaps list items to a
// SnapPosition.

#include "kiln/animation/Decay.h"
#include "kiln/foundation/Lazy.h"

namespace kiln::foundation {

    // Where an item rests: Start, Center or End of the viewport (between the content padding),
    // or a custom offset that may depend on the item (a pager page's index and the page count).
    struct SnapPosition {
        using Fn = std::function<int(int layoutSize, int itemSize, int beforeContentPadding, int afterContentPadding,
                                     int itemIndex, int itemCount)>;
        enum class Kind : std::uint8_t { Start, Center, End } kind = Kind::Center;
        Fn custom;
        static SnapPosition Start() { return { Kind::Start, {} }; }
        static SnapPosition Center() { return { Kind::Center, {} }; }
        static SnapPosition End() { return { Kind::End, {} }; }
        static SnapPosition Custom(Fn f) { return { Kind::Start, std::move(f) }; }
        int Position(int layoutSize, int itemSize, int beforeContentPadding, int afterContentPadding,
                     int itemIndex = 0, int itemCount = 0) const;
    };

    class SnapLayoutInfoProvider {
    public:
        virtual ~SnapLayoutInfoProvider() = default;
        // How far to travel before snapping, given what the decay would cover.
        virtual float CalculateApproachOffset(float velocity, float decayOffset) { (void)velocity; return decayOffset; }
        // The offset to the snap target from where the approach ended.
        virtual float CalculateSnapOffset(float velocity) = 0;
    };

    // snapFlingBehavior: `decay` is resolved when a fling starts (it may need the density).
    FlingBehavior SnapFlingBehavior(std::shared_ptr<SnapLayoutInfoProvider> provider,
                                    std::function<std::shared_ptr<const anim::DecaySpec>()> decay,
                                    anim::SpecPtr snapAnimationSpec = anim::Spring(1.0f, anim::spring::StiffnessMediumLow));
    // The same fling as a TargetedFlingBehavior: the distance left to the approach target, then
    // to the snap target, each frame.
    TargetedFlingBehavior TargetedSnapFlingBehavior(std::shared_ptr<SnapLayoutInfoProvider> provider,
                                                    std::function<std::shared_ptr<const anim::DecaySpec>()> decay,
                                                    anim::SpecPtr snapAnimationSpec = anim::Spring(1.0f, anim::spring::StiffnessMediumLow));

    // SnapLayoutInfoProvider(lazyListState / lazyGridState, snapPosition): approach to an item
    // short of the decay, then snap to the closest item (or the next / previous one past 400 dp/s).
    std::shared_ptr<SnapLayoutInfoProvider> MakeSnapLayoutInfoProvider(LazyListState& state, SnapPosition position = SnapPosition::Center());
    std::shared_ptr<SnapLayoutInfoProvider> MakeSnapLayoutInfoProvider(LazyGridState& state, SnapPosition position = SnapPosition::Center());

    // rememberSnapFlingBehavior(lazyListState, snapPosition): the platform spline decay and
    // spring(StiffnessMediumLow).
    FlingBehavior RememberSnapFlingBehavior(LazyListState& state, SnapPosition position = SnapPosition::Center(), rt::Key key = {});
    FlingBehavior RememberSnapFlingBehavior(LazyGridState& state, SnapPosition position = SnapPosition::Center(), rt::Key key = {});

}
