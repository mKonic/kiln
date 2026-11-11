#pragma once

// Semantics modifiers (Modifier.semantics / clearAndSetSemantics).

#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    using ui::Modifier;
    using ui::SemanticsConfiguration;

    Modifier Semantics(Modifier m, std::function<void(SemanticsConfiguration&)> properties, bool mergeDescendants = false);
    Modifier ClearAndSetSemantics(Modifier m, std::function<void(SemanticsConfiguration&)> properties);

    // Modifier.progressSemantics(value, valueRange, steps): a determinate progress range, the value
    // coerced into it; merges descendants (older TalkBack skips range info on unfocusable nodes).
    Modifier ProgressSemantics(Modifier m, float value, float start = 0, float end = 1, int steps = 0);
    // Modifier.progressSemantics(): an operation in progress (indeterminate).
    Modifier ProgressSemantics(Modifier m);

}
