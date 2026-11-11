#pragma once

// MyersDiff.kt (compose ui node): Eugene Myers' diff, as the modifier chain update runs it to
// match the old chain's nodes with the new chain's elements. Reports each kept pair in order.

#include <functional>

namespace kiln::ui {

    // Calls same(oldIndex, newIndex) for every matched pair, in increasing order; what is not
    // matched is removed (old) or inserted (new).
    void ExecuteDiff(int oldSize, int newSize, const std::function<bool(int oldIndex, int newIndex)>& areItemsTheSame,
                     const std::function<void(int oldIndex, int newIndex)>& same);

}
