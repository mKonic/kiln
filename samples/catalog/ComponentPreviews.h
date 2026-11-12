#pragma once

#include <functional>
#include <string>

namespace catalog {
    // The live preview a component's home screen tile shows (ComponentPreviews.kt), or null.
    std::function<void()> ComponentPreview(const std::string& componentName);
}
