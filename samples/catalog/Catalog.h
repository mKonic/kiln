#pragma once

// The androidx material3 catalog (integration-tests/material3-catalog) on kiln: the same
// components, examples and screens, each example a port of its material3 sample, so a kiln
// screen can be set beside the Compose one.

#include <string>
#include <vector>

namespace catalog {

    // Example (model/Examples.kt). content is null while the sample is not ported yet.
    struct Example {
        std::string name, description;
        bool isExpressive = false;
        void (*content)() = nullptr;
    };

    // Component (model/Components.kt); ids count from 1 in list order, as nextId() does.
    struct Component {
        int id = 0;
        std::string name, description;
        bool tintIcon = true;
        // Not 0: every example shows in a box this wide (Examples.kt wraps the text field
        // samples in Box(Modifier.wrapContentWidth().width(280.dp))).
        int exampleWidthDp = 0;
        std::vector<Example> examples;
        bool HasExpressiveExamples() const {
            for (const auto& e : examples)
                if (e.isExpressive) return true;
            return false;
        }
    };

    // Registry.gen.cpp, written by gen-registry.
    const std::vector<Component>& Components();

}
