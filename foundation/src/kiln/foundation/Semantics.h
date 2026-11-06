#pragma once

// Semantics modifiers (Modifier.semantics / clearAndSetSemantics).

#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    using ui::Modifier;
    using ui::SemanticsConfiguration;

    Modifier Semantics(Modifier m, std::function<void(SemanticsConfiguration&)> properties, bool mergeDescendants = false);
    Modifier ClearAndSetSemantics(Modifier m, std::function<void(SemanticsConfiguration&)> properties);

}
