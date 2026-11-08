#pragma once

// material3 internal/ChildParentSemantics.kt: a parent's semantics properties (on a node that
// merges its descendants) move to the nearest child that asks for them while it is attached.

#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Semantics.h"

#include <functional>

namespace kiln::material3::internal {

    using SemanticsProperties = std::function<void(ui::SemanticsConfiguration&)>;
    ui::Modifier ChildSemantics(ui::Modifier m, SemanticsProperties properties = {});
    ui::Modifier ParentSemantics(ui::Modifier m, SemanticsProperties properties);

}
