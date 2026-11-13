#pragma once

#include <functional>
#include <kiln/Function.h>
#include <string>

namespace catalog {
    // The live preview a component's home screen tile shows (ComponentPreviews.kt), or null.
    kiln::Function<void()> ComponentPreview(const std::string& componentName);
}
