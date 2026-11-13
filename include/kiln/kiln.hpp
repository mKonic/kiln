#pragma once
// C++ entry to kiln's declarative layer. A C++ app still exports kiln_main and calls
// kiln::SetContent from it.
#include <kiln/kiln.h>
#include <kiln/Function.h>

#include <functional>

namespace kiln {
    void SetContent(kiln_app* app, kiln::Function<void()> content);
}
