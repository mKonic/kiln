#pragma once

// Semantics modifiers (Modifier.semantics / clearAndSetSemantics).

#include "kiln/ui/LayoutNode.h"
#include <kiln/Function.h>

namespace kiln::foundation {

    using ui::Modifier;
    using ui::SemanticsConfiguration;

    Modifier Semantics(Modifier m, kiln::Function<void(SemanticsConfiguration&)> properties, bool mergeDescendants = false);
    Modifier ClearAndSetSemantics(Modifier m, kiln::Function<void(SemanticsConfiguration&)> properties);
    // Modifier.testTag: a tag tests find the element by (kiln/test, OnNodeWithTag); it is not
    // read by accessibility unless testTagsAsResourceId is set.
    inline Modifier TestTag(Modifier m, std::string tag) {
        return Semantics(std::move(m), [tag = std::move(tag)](SemanticsConfiguration& c) { c.testTag = tag; });
    }

    // Modifier.progressSemantics(value, valueRange, steps): a determinate progress range, the value
    // coerced into it; merges descendants (older TalkBack skips range info on unfocusable nodes).
    Modifier ProgressSemantics(Modifier m, float value, float start = 0, float end = 1, int steps = 0);
    // Modifier.progressSemantics(): an operation in progress (indeterminate).
    Modifier ProgressSemantics(Modifier m);

}
