#pragma once

// Shared by the sample ports: the namespaces they use and Compose modifier spellings kiln
// writes another way.

#include "../gen/Icons.h"

#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Layouts.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/runtime/Saveable.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Text.h>
#include <kiln/runtime/Composition.h>

namespace catalog::samples {
    using namespace kiln::foundation;
    using namespace kiln::ui;
    namespace m3 = kiln::material3;
    using catalog::Icons::ImageVectorPtr;
    namespace Icons = catalog::Icons;
    using kiln::rt::RememberSaveableState;

    // Modifier.size(size)
    inline Modifier Size(Modifier m, Dp size) { return SizeMod(std::move(m), size, size); }
    // Modifier.heightIn(min, max)
    inline Modifier HeightIn(Modifier m, Dp min, Dp max = Dp::Unspecified()) {
        return SizeIn(std::move(m), Dp::Unspecified(), min, Dp::Unspecified(), max);
    }
    // Modifier.widthIn(min, max)
    inline Modifier WidthIn(Modifier m, Dp min, Dp max = Dp::Unspecified()) {
        return SizeIn(std::move(m), min, Dp::Unspecified(), max, Dp::Unspecified());
    }
    // Text(text, style = ...)
    inline void TextStyled(const std::string& text, const kiln::text::TextStyle& style, const Modifier& modifier = {}) {
        m3::TextOptions o;
        o.style = style;
        m3::Text(text, modifier, o);
    }
}

namespace catalog::samples {
    // Modifier.semantics { contentDescription = ... }
    inline Modifier ContentDescription(Modifier m, std::string description) {
        return Semantics(std::move(m), [description](SemanticsConfiguration& c) { c.contentDescription = { description }; });
    }
    // The current composition (for Read / Remember).
    inline kiln::rt::Composition& Here() { return *kiln::rt::Composition::Current(); }
}

#include <kiln/runtime/CoroutineScope.h>

namespace catalog::samples {
    // rememberCoroutineScope, as a value that lambdas copy.
    struct LaunchScope {
        std::shared_ptr<kiln::rt::CoroutineScope> scope;
        void Launch(kiln::rt::Task<> task) const { scope->Launch(std::move(task)); }
    };
    inline LaunchScope RememberCoroutineScope(kiln::rt::Key key = {}) { return LaunchScope{ kiln::rt::RememberCoroutineScope(key) }; }
}

namespace catalog::samples {
    // rememberSaveable { mutableStateListOf(...) } for ints and booleans.
    template<typename T>
        requires(std::is_same_v<T, int> || std::is_same_v<T, bool>)
    kiln::rt::State<std::vector<T>>& RememberSaveableList(std::vector<T> initial, kiln::rt::Key key = {}) {
        return RememberSaveableState(std::move(initial), kiln::rt::ListSaver<std::vector<T>>(
            [](const std::vector<T>& v) {
                kiln::rt::SavedValue::List l;
                for (T x : v) l.push_back(kiln::rt::SavedValue(x));
                return l;
            },
            [](const kiln::rt::SavedValue::List& l) -> std::optional<std::vector<T>> {
                std::vector<T> v;
                for (const auto& s : l) {
                    if constexpr (std::is_same_v<T, bool>) {
                        if (auto* b = std::get_if<bool>(&s.v)) v.push_back(*b);
                    } else if (auto* i = std::get_if<std::int64_t>(&s.v)) {
                        v.push_back(int(*i));
                    }
                }
                return v;
            }), key);
    }
}
