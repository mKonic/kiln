#pragma once

#include "kiln/Internal.h"
// SwipeToDismissBox.kt: content that can be swiped away horizontally (start to end, end to
// start, or both), showing its background as it moves; onDismiss runs once it settles away.

#include "kiln/foundation/AnchoredDraggable.h"
#include <kiln/Function.h>
#include "kiln/runtime/Task.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>

namespace kiln::material3 {

    enum class SwipeToDismissBoxValue : std::uint8_t { StartToEnd, EndToStart, Settled };

    class SwipeToDismissBoxState {
    public:
        // positionalThreshold: how far (of the total distance, in px) a release must have moved
        // to dismiss. confirmValueChange: the deprecated veto (vetoed swipes settle back).
        SwipeToDismissBoxState(SwipeToDismissBoxValue initialValue, kiln::Function<float(float totalDistance)> positionalThreshold,
                               kiln::Function<bool(SwipeToDismissBoxValue)> confirmValueChange = {});

        float RequireOffset() const { return m_Anchored.RequireOffset(); }
        // Reading these subscribes.
        SwipeToDismissBoxValue CurrentValue() const { return m_Anchored.CurrentValue(); }
        SwipeToDismissBoxValue TargetValue() const { return m_Anchored.TargetValue(); }
        SwipeToDismissBoxValue SettledValue() const { return m_Anchored.SettledValue(); }
        // From the current value towards the closest anchor, 0..1.
        float Progress() const;
        // Which way the content is moved: Settled at rest, StartToEnd right of it, EndToStart left.
        SwipeToDismissBoxValue DismissDirection() const;

        rt::Task<> SnapTo(SwipeToDismissBoxValue target);
        rt::Task<> Reset();
        rt::Task<> Dismiss(SwipeToDismissBoxValue direction);

        // Internal.
        KILN_INTERNAL foundation::AnchoredDraggableState<SwipeToDismissBoxValue>& Anchored() { return m_Anchored; }
        KILN_INTERNAL const foundation::AnchoredDraggableState<SwipeToDismissBoxValue>& Anchored() const { return m_Anchored; }
        const kiln::Function<float(float)>& PositionalThreshold() const { return m_PositionalThreshold; }

    private:
        kiln::Function<float(float)> m_PositionalThreshold;
        foundation::AnchoredDraggableState<SwipeToDismissBoxValue> m_Anchored;
    };

    namespace SwipeToDismissBoxDefaults {
        // 56 dp, whatever the distance.
        kiln::Function<float(float totalDistance)> positionalThreshold();
    }

    // rememberSwipeToDismissBoxState: the value survives re-creation. Null threshold: the default.
    std::shared_ptr<SwipeToDismissBoxState> RememberSwipeToDismissBoxState(SwipeToDismissBoxValue initialValue = SwipeToDismissBoxValue::Settled,
                                                                           kiln::Function<float(float)> positionalThreshold = {},
                                                                           kiln::Function<bool(SwipeToDismissBoxValue)> confirmValueChange = {}, rt::Key key = {});

    struct SwipeToDismissBoxOptions {
        bool enableDismissFromStartToEnd = true, enableDismissFromEndToStart = true;
        bool gesturesEnabled = true;
        kiln::Function<void(SwipeToDismissBoxValue)> onDismiss;
    };
    // backgroundContent fills the box behind content (both are rows).
    void SwipeToDismissBox(std::shared_ptr<SwipeToDismissBoxState> state, kiln::Function<void()> backgroundContent, const ui::Modifier& modifier,
                           const SwipeToDismissBoxOptions& options, kiln::Function<void()> content);

}
